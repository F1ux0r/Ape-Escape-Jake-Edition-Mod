#include "cpu_state.h"
#include "mod_plugins.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Play as Jake.
 *
 * Spike is drawn with Jake's mesh everywhere, and Jake (in the levels he is
 * in) with Spike's. Only the look changes: each keeps his own skeleton,
 * animation, controls and AI.
 *
 * Characters are segmented meshes drawn by 0x80016A60 (transform) and an
 * emitter, from an object whose first five words are the mesh: header
 * (u16 colour count at +8, part count at +4, vertex count at +6),
 * colours, polys, part table (12 bytes a part: vertex count, first vertex,
 * bone offset, parent + 1) and vertices. Word +0x14 points at the bone
 * matrices (part i uses matrix i + 1, 0x20 bytes each), already posed by
 * the time the transform runs.
 *
 * Jake's mesh is only in RAM in his own levels, so it is read from the
 * user's disc at start (KKIIDDZZ.DAT; the package targets one disc image,
 * so its offset is fixed, and its header is checked) into mod memory. His
 * face texture (a 64x64 4-bit image, nibble run-length packed: a byte with
 * equal nibbles is that pixel count + 2 times, else two pixels low first)
 * is unpacked and kept uploaded to a VRAM strip that is free in every level
 * checked, and the mesh's textured polys are pointed at it.
 *
 * Spike and Jake share parts 0-15 (same bone offsets and parents). Jake has
 * two more, a hair tail chained off the head (16 on part 3, 17 on 16).
 * Drawing Jake's mesh on Spike, those two matrices are made from Spike's
 * head and the tail's pose relative to the head (Jake's live pose when he
 * is in the level, else his usual one).
 *
 * The mesh words are swapped at the transform's entry and put back at
 * DrawOTag (0x80028CC0), so the animation code, which runs before the
 * transform, only ever sees each character's own mesh.
 */

static const uint32_t JAKE_SPIKE_OBJECT = 0x800EC1E0u;
static const uint32_t JAKE_TRANSFORM_FUNCTION = 0x80016A60u;
static const uint32_t JAKE_DRAW_OT_FUNCTION = 0x80028CC0u;
/* The ordering table of each frame buffer and the game's
 * packet buffer pointer, as the model emitter uses them. */
static const uint32_t JAKE_ORDERING_TABLE = 0x800EC3D0u;
static const uint32_t JAKE_ORDERING_TABLE_STRIDE = 0x4078u;
/* Our own GPU-DMA memory per buffer, never the game's packet buffer
 * (which the game may already be refilling while the GPU reads it). */
enum { JAKE_ARENA_SIZE = 0x1000 };
static uint32_t g_arena[2], g_arena_used;
static int g_arena_index;

/* Disc user-data offsets (2048-byte sectors) in KKIIDDZZ.DAT. */
static const uint32_t JAKE_DISC_MESH = 0x033E23ECu;
static const uint32_t JAKE_DISC_MESH_SIZE = 0x1E38u;
static const uint32_t JAKE_DISC_CLUT = 0x033BDCB0u;            /* 16 colours */
static const uint32_t JAKE_DISC_IMAGE = 0x033BDCB0u + 0x2Cu;   /* packed */
static const uint32_t JAKE_DISC_IMAGE_SIZE = 0x1C4u - 12u;
static const uint8_t JAKE_MESH_SIGNATURE[12] = {
    0x20, 0x00, 0x36, 0x01, 0x12, 0x00, 0x25, 0x01, 0x24, 0x00, 0x00, 0x00
};

/* Where the face goes: page 15 (x 960), texels at x 984..999, y 128..191,
 * palette at (960, 255). The stock texture is page 9 with the image at its
 * origin, so u moves by 96 and v by 128. */
enum {
    JAKE_VRAM_X = 984, JAKE_VRAM_Y = 128, JAKE_VRAM_W = 16, JAKE_VRAM_H = 64,
    JAKE_CLUT_X = 960, JAKE_CLUT_Y = 255,
    JAKE_PAGE = 15, JAKE_U_SHIFT = 96, JAKE_V_SHIFT = 128,
    JAKE_UPLOAD_PERIOD = 8
};

enum {
    JAKE_MESH_WORDS = 5,
    JAKE_SPIKE_PARTS = 16,
    JAKE_JAKE_PARTS = 18,
    JAKE_HEAD_PART = 3,
    JAKE_TAIL_PART = 16,     /* 16 and 17 */
    JAKE_PART_ENTRY_SIZE = 12,
    JAKE_MATRIX_SIZE = 0x20,
    JAKE_MATRIX_TRANSLATION = 20
};

/*
 * Spike on the HUD. Each entry matches a sprite (texture page word, then a
 * 0x64..0x67 sprite: colour, xy, clut<<16 | uv, size) by page, uv and
 * palette and redraws it from new_uv, with a recoloured copy of the palette
 * kept in free VRAM (clut_x/clut_y; 0 = keep the stock palette):
 *  - L2 face (page 0x17): hair red -> Jake's purple.
 *  - pause menu lives shirt (page 0x4F): red -> his navy.
 *  - race map heads (page 0x26, palette 0x3F9B; Spike at uv F860, Jake at
 *    F868): swapped like the bodies, Jake's blue hair made purple.
 * The palettes the game loaded are left alone, so nothing else that shares
 * them changes.
 */
typedef struct JakeHudIcon {
    uint16_t tpage, uv, stock_clut, new_uv, clut_x, clut_y;
    uint16_t palette[16];
} JakeHudIcon;
static const JakeHudIcon JAKE_HUD_ICONS[] = {
    { 0x17u, 0x8060u, 0x7E1Du, 0x8060u, 976u, 255u, {
      0x0000, 0xBDF0, 0x94C5, 0xBC68, 0xD8CD, 0xE931, 0xB21E, 0xBA7D,
      0xE739, 0xA170, 0xB1F8, 0xC6BE, 0x8EFF, 0xD6B5, 0xF9F2, 0xFFFF } },
    { 0x4Fu, 0x60C0u, 0x3AFFu, 0x60C0u, 992u, 255u, {
      0x0000, 0x18A6, 0x1863, 0x24A5, 0x2CC6, 0x34E7, 0x4129, 0x6A6E,
      0x7FBB, 0x4D6B, 0x72D5, 0x7B57, 0x8424, 0x1463, 0x61ED, 0x7FFF } },
    { 0x26u, 0xF860u, 0x3F9Bu, 0xF868u, 976u, 254u, {
      0x0000, 0x9CA4, 0xD8CD, 0xE931, 0x8C73, 0x801A, 0x801F, 0x931C,
      0x9BFF, 0x9D92, 0xA218, 0xBEBB, 0xCF3F, 0xBC68, 0xFA15, 0xFFDE } },
    { 0x26u, 0xF868u, 0x3F9Bu, 0xF860u, 0u, 0u, { 0 } },
};
enum { JAKE_HUD_ICON_COUNT = sizeof(JAKE_HUD_ICONS) / sizeof(JAKE_HUD_ICONS[0]) };

typedef struct JakeAffine {
    float r[3][3];
    float t[3];
} JakeAffine;

/* Function-entry hooks run whether or not the feature is selected; the
 * activation callback (only called when it is) turns them on. */
static uint8_t g_enabled;

/* Jake's mesh from the disc, and once copied into guest memory the five
 * mesh words that point at it. */
static uint8_t g_disc_mesh[0x1E38];
static uint16_t g_face_clut[16];
static uint16_t g_face_texels[JAKE_VRAM_W * JAKE_VRAM_H];
static uint8_t g_disc_ok;
static uint32_t g_jake_mesh[JAKE_MESH_WORDS];
static uint8_t g_jake_mesh_ready;

/* Spike's own mesh words, as last seen unswapped. */
static uint32_t g_spike_mesh[JAKE_MESH_WORDS];
static uint8_t g_have_spike;

/* Objects whose mesh words are swapped this frame, to restore. */
static uint32_t g_swapped[8];
static uint32_t g_swapped_words[8][JAKE_MESH_WORDS];
static int g_swapped_count;

/* Jake's hair tail relative to his head: live while he is in the level,
 * else his usual pose (read off him in his race, the run's side-to-side
 * sway centred): bone 16 turned 76 degrees to hang down the back of the
 * head, 17 below it. */
static const JakeAffine JAKE_TAIL_REST[2] = {
    { { { 0.24f, 0.97f, 0.0f }, { -0.97f, 0.24f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
      { -19.0f, 0.0f, 0.0f } },
    { { { -0.87f, 0.48f, 0.0f }, { -0.48f, -0.87f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
      { -8.5f, -40.5f, 0.0f } },
};
static JakeAffine g_tail_rel[2];
static uint32_t g_tail_frame, g_frame;
static uint8_t g_have_tail;

/* ---- Disc -------------------------------------------------------------- */

/* The .bin next to the .cue named by disc.cfg (the runtime's own config). */
static int jake_disc_path(char* out, size_t size) {
    char cue[1024], line[1024];
    FILE* f = fopen("disc.cfg", "rb");
    size_t n;
    char* slash;
    if (!f)
        return 0;
    n = fread(cue, 1, sizeof(cue) - 1, f);
    fclose(f);
    cue[n] = '\0';
    while (n > 0 && (cue[n - 1] == '\r' || cue[n - 1] == '\n' || cue[n - 1] == ' '))
        cue[--n] = '\0';
    f = fopen(cue, "rb");
    if (!f)
        return 0;
    out[0] = '\0';
    while (fgets(line, sizeof(line), f)) {
        char* q0 = strchr(line, '"');
        char* q1 = q0 ? strchr(q0 + 1, '"') : NULL;
        if (strncmp(line, "FILE", 4) != 0 || !q1)
            continue;
        *q1 = '\0';
        slash = strrchr(cue, '\\');
        if (!slash)
            slash = strrchr(cue, '/');
        if (slash)
            snprintf(out, size, "%.*s%s", (int)(slash - cue + 1), cue, q0 + 1);
        else
            snprintf(out, size, "%s", q0 + 1);
        break;
    }
    fclose(f);
    return out[0] != '\0';
}

/* Mode 2 Form 1 user data at a user-data byte offset. */
static int jake_read_disc(FILE* f, uint32_t offset, uint8_t* out, uint32_t size) {
    while (size > 0) {
        const uint32_t sector = offset / 2048u, within = offset % 2048u;
        uint32_t chunk = 2048u - within;
        if (chunk > size)
            chunk = size;
        if (fseek(f, (long)sector * 2352L + 24L + (long)within, SEEK_SET) != 0 ||
            fread(out, 1, chunk, f) != chunk)
            return 0;
        out += chunk;
        offset += chunk;
        size -= chunk;
    }
    return 1;
}

static void jake_unpack_face(const uint8_t* packed, uint32_t size) {
    uint8_t pixels[JAKE_VRAM_W * 4 * JAKE_VRAM_H];
    uint32_t i = 0, n = 0, k;
    memset(pixels, 0, sizeof(pixels));
    while (i < size && n < sizeof(pixels)) {
        const uint8_t b = packed[i++], lo = b & 15u, hi = b >> 4;
        if (lo == hi) {
            uint32_t run = (i < size ? packed[i++] : 0u) + 2u;
            while (run-- > 0u && n < sizeof(pixels))
                pixels[n++] = lo;
        } else {
            pixels[n++] = lo;
            if (n < sizeof(pixels))
                pixels[n++] = hi;
        }
    }
    for (k = 0; k < JAKE_VRAM_W * JAKE_VRAM_H; ++k)
        g_face_texels[k] = (uint16_t)(pixels[k * 4] | (pixels[k * 4 + 1] << 4) |
                                      (pixels[k * 4 + 2] << 8) | (pixels[k * 4 + 3] << 12));
}

static void jake_load_disc(void) {
    char path[1024];
    uint8_t clut[32], packed[0x1C4];
    FILE* f;
    int k;
    if (!jake_disc_path(path, sizeof(path)) || !(f = fopen(path, "rb"))) {
        fprintf(stderr, "play-as-jake: disc image not found\n");
        return;
    }
    if (jake_read_disc(f, JAKE_DISC_MESH, g_disc_mesh, JAKE_DISC_MESH_SIZE) &&
        jake_read_disc(f, JAKE_DISC_CLUT, clut, sizeof(clut)) &&
        jake_read_disc(f, JAKE_DISC_IMAGE, packed, JAKE_DISC_IMAGE_SIZE) &&
        memcmp(g_disc_mesh, JAKE_MESH_SIGNATURE, sizeof(JAKE_MESH_SIGNATURE)) == 0) {
        for (k = 0; k < 16; ++k)
            g_face_clut[k] = (uint16_t)(clut[k * 2] | (clut[k * 2 + 1] << 8));
        jake_unpack_face(packed, JAKE_DISC_IMAGE_SIZE);
        g_disc_ok = 1u;
    } else {
        fprintf(stderr, "play-as-jake: Jake's model was not found on this disc\n");
    }
    fclose(f);
}

/* ---- Voice ------------------------------------------------------------- */

/*
 * Spike's double-jump yell becomes Jake's jump yell (the one he gives
 * leaping into the water in his race). Spike's yell is SPU sample 0x38F00
 * (0xC30 bytes; keyed at pitch 0x4B8, ~13 kHz); Jake's is in his race's
 * sound bank (disc user offset 0x2E9948, 0xDE0 bytes, pitch 0x5D2, ~16 kHz),
 * only loaded there. At start Jake's is read from the disc, decoded,
 * resampled to Spike's playback rate (so he keeps his own pitch) and
 * re-encoded, which also makes it short enough (0xB50 bytes) to fit in
 * Spike's slot. Every half second, if SPU RAM holds Spike's yell (a level
 * load puts it back), Jake's is written over it through the SPU's manual
 * transfer port; anything else there is left alone.
 */
uint32_t spu_ram_peek(uint32_t addr, uint8_t* out, uint32_t len);

static const uint32_t JAKE_DISC_VOICE = 0x002E9948u;
static const uint32_t SPIKE_DISC_YELL = 0x00159EF0u;
enum {
    JAKE_VOICE_SIZE = 0xDE0,
    SPIKE_YELL_SPU = 0x38F00,
    SPIKE_YELL_SIZE = 0xC30,
    JAKE_VOICE_PITCH = 0x5D2,
    SPIKE_YELL_PITCH = 0x4B8,
    JAKE_VOICE_CHECK_PERIOD = 30
};
static const uint32_t SPU_TRANSFER_ADDRESS = 0x1F801DA6u;
static const uint32_t SPU_TRANSFER_FIFO = 0x1F801DA8u;

static uint8_t g_spike_yell_head[64];   /* first data blocks, to recognise it */
static uint8_t g_jake_yell[SPIKE_YELL_SIZE];
static uint32_t g_jake_yell_size;

static const int JAKE_ADPCM_FILTERS[5][2] = {
    { 0, 0 }, { 60, 0 }, { 115, -52 }, { 98, -55 }, { 122, -60 }
};

static int16_t jake_clamp16(int v) {
    return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

/* SPU ADPCM blocks -> PCM. Returns the sample count. */
static uint32_t jake_adpcm_decode(const uint8_t* in, uint32_t size, int16_t* out, uint32_t cap) {
    int s1 = 0, s2 = 0;
    uint32_t n = 0, b;
    for (b = 0; b + 16u <= size; b += 16u) {
        const int shift = in[b] & 15, filter = (in[b] >> 4) & 7;
        const int f0 = JAKE_ADPCM_FILTERS[filter > 4 ? 4 : filter][0];
        const int f1 = JAKE_ADPCM_FILTERS[filter > 4 ? 4 : filter][1];
        int j;
        for (j = 0; j < 28 && n < cap; ++j) {
            int nib = (in[b + 2u + (uint32_t)(j / 2)] >> ((j & 1) * 4)) & 15;
            int v;
            if (nib >= 8)
                nib -= 16;
            v = ((nib << 12) >> shift) + ((s1 * f0 + s2 * f1 + 32) >> 6);
            out[n++] = jake_clamp16(v);
            s2 = s1;
            s1 = out[n - 1];
        }
        if (in[b + 1u] & 1u)
            break;
    }
    return n;
}

/* One 28-sample block: the filter and shift with the least error. */
static void jake_adpcm_block(const int16_t* x, int* s1, int* s2, uint8_t flags, uint8_t out[16]) {
    long long best = -1;
    int bf = 0, bs = 0, f, sh, j;
    for (f = 0; f < 5; ++f) {
        for (sh = 0; sh <= 12; ++sh) {
            int a = *s1, b = *s2;
            long long err = 0;
            for (j = 0; j < 28; ++j) {
                const int pred = (a * JAKE_ADPCM_FILTERS[f][0] + b * JAKE_ADPCM_FILTERS[f][1] + 32) >> 6;
                int nib = (int)(((long long)(x[j] - pred) * (1LL << sh)) + (x[j] - pred >= 0 ? 2048 : -2048)) / 4096;
                int v;
                nib = nib < -8 ? -8 : nib > 7 ? 7 : nib;
                v = jake_clamp16(((nib << 12) >> sh) + pred);
                err += (long long)(x[j] - v) * (x[j] - v);
                b = a;
                a = v;
            }
            if (best < 0 || err < best) {
                best = err;
                bf = f;
                bs = sh;
            }
        }
    }
    out[0] = (uint8_t)((bf << 4) | bs);
    out[1] = flags;
    memset(out + 2, 0, 14);
    for (j = 0; j < 28; ++j) {
        const int pred = (*s1 * JAKE_ADPCM_FILTERS[bf][0] + *s2 * JAKE_ADPCM_FILTERS[bf][1] + 32) >> 6;
        int nib = (int)(((long long)(x[j] - pred) * (1LL << bs)) + (x[j] - pred >= 0 ? 2048 : -2048)) / 4096;
        nib = nib < -8 ? -8 : nib > 7 ? 7 : nib;
        out[2 + j / 2] |= (uint8_t)((nib & 15) << ((j & 1) * 4));
        *s2 = *s1;
        *s1 = jake_clamp16(((nib << 12) >> bs) + pred);
    }
}

static void jake_build_voice(const uint8_t* disc) {
    static int16_t pcm[JAKE_VOICE_SIZE / 16 * 28], resampled[JAKE_VOICE_SIZE / 16 * 28];
    /* Skip the source's silent lead-in block; ours is added below. */
    const uint32_t n = jake_adpcm_decode(disc + 16, JAKE_VOICE_SIZE - 16u, pcm, sizeof(pcm) / sizeof(pcm[0]));
    const uint32_t m = (uint32_t)((uint64_t)n * SPIKE_YELL_PITCH / JAKE_VOICE_PITCH);
    const uint32_t blocks = (m + 27u) / 28u;
    uint32_t i, b;
    int s1 = 0, s2 = 0;
    if (16u * (blocks + 1u) > SPIKE_YELL_SIZE)
        return;
    for (i = 0; i < blocks * 28u; ++i) {
        const float at = (float)i * JAKE_VOICE_PITCH / SPIKE_YELL_PITCH;
        const uint32_t k = (uint32_t)at;
        const float t = at - (float)k;
        resampled[i] = i >= m || k + 1u >= n ? 0
            : (int16_t)((float)pcm[k] * (1.0f - t) + (float)pcm[k + 1u] * t);
    }
    memset(g_jake_yell, 0, 16);   /* the silent lead-in block, as stock */
    for (b = 0; b < blocks; ++b)
        jake_adpcm_block(resampled + b * 28u, &s1, &s2,
                         (uint8_t)(b == 0 ? 0x04u : b + 1u == blocks ? 0x01u : 0x00u),
                         g_jake_yell + 16u * (b + 1u));
    g_jake_yell_size = 16u * (blocks + 1u);
}

static void jake_install_voice(void) {
    uint8_t head[sizeof(g_spike_yell_head)];
    const uint16_t saved = psx_mod_read_half(SPU_TRANSFER_ADDRESS);
    uint32_t i;
    if (g_jake_yell_size == 0u ||
        spu_ram_peek(SPIKE_YELL_SPU, head, sizeof(head)) != sizeof(head) ||
        memcmp(head, g_spike_yell_head, sizeof(head)) != 0)
        return;
    psx_mod_write_half(SPU_TRANSFER_ADDRESS, (uint16_t)(SPIKE_YELL_SPU >> 3));
    for (i = 0; i < g_jake_yell_size; i += 2u)
        psx_mod_write_half(SPU_TRANSFER_FIFO,
                           (uint16_t)(g_jake_yell[i] | (g_jake_yell[i + 1u] << 8)));
    psx_mod_write_half(SPU_TRANSFER_ADDRESS, saved);
}

static void jake_vblank(void) {
    static uint32_t ticks;
    if (g_enabled && ticks++ % JAKE_VOICE_CHECK_PERIOD == 0u)
        jake_install_voice();
}

/* ---- Mesh -------------------------------------------------------------- */

static uint16_t jake_get16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static void jake_put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* Point the textured polys at the uploaded face. Polys are 0x1C bytes
 * (textured: 0x24/0x2C/0x34/0x3C, uv0+clut at +12, uv1+tpage at +16, uv2
 * at +20, uv3 at +22) or 0x10 bytes (untextured 0x20/0x28/0x30/0x38). */
static int jake_retarget_polys(uint8_t* mesh, uint32_t polys, uint32_t end, uint32_t count) {
    uint32_t i, at = polys;
    for (i = 0; i < count; ++i) {
        const uint8_t code = mesh[at + 3] & 0xFCu;
        const int textured = code == 0x24 || code == 0x2C || code == 0x34 || code == 0x3C;
        const uint32_t size = textured ? 0x1Cu : 0x10u;
        if ((code & 0xE0u) != 0x20u || at + size > end)
            return 0;
        if (textured) {
            const int quad = code == 0x2C || code == 0x3C;
            const uint16_t tpage = jake_get16(mesh + at + 18);
            static const uint8_t uv_at[4] = { 12, 16, 20, 22 };
            int v;
            jake_put16(mesh + at + 14, (uint16_t)((JAKE_CLUT_Y << 6) | (JAKE_CLUT_X >> 4)));
            jake_put16(mesh + at + 18, (uint16_t)((tpage & ~0x1Fu) | JAKE_PAGE));
            for (v = 0; v < (quad ? 4 : 3); ++v) {
                mesh[at + uv_at[v]] = (uint8_t)(mesh[at + uv_at[v]] + JAKE_U_SHIFT);
                mesh[at + uv_at[v] + 1] = (uint8_t)(mesh[at + uv_at[v] + 1] + JAKE_V_SHIFT);
            }
        }
        at += size;
    }
    return 1;
}

/*
 * Copy Jake's mesh into guest memory. Game code reaches into the player's
 * mesh by vertex number too (the slingshot band takes Spike's vertex 325,
 * on his hand), and Jake has fewer vertices (293 to Spike's 328), so his
 * vertex list is followed by Spike's own vertices from 293 on: any such
 * index still lands on Spike's matching point instead of on polys.
 */
static void jake_prepare_mesh(uint32_t spike_object) {
    static uint8_t tried;
    uint8_t mesh[0x1E38];
    uint32_t base, colours, parts, verts, polys, k, jake_verts, spike_verts, vert_list;
    if (tried || !g_disc_ok)
        return;
    tried = 1u;
    memcpy(mesh, g_disc_mesh, sizeof(mesh));
    colours = 0x0Cu;
    parts = colours + jake_get16(mesh + 8) * 4u;
    verts = parts + jake_get16(mesh + 4) * JAKE_PART_ENTRY_SIZE;
    polys = verts + jake_get16(mesh + 6) * 8u;
    if (polys >= sizeof(mesh) ||
        !jake_retarget_polys(mesh, polys, sizeof(mesh), jake_get16(mesh + 2))) {
        fprintf(stderr, "play-as-jake: Jake's mesh did not parse\n");
        return;
    }
    jake_verts = jake_get16(mesh + 6);
    spike_verts = psx_mod_read_half(psx_mod_read_word(spike_object) + 6u);
    if (spike_verts < jake_verts)
        spike_verts = jake_verts;
    base = psx_mod_alloc_guest_memory(sizeof(mesh) + spike_verts * 8u, 16u);
    if (base == 0u)
        return;
    for (k = 0; k < sizeof(mesh); k += 4)
        psx_mod_write_word(base + k, (uint32_t)mesh[k] | ((uint32_t)mesh[k + 1] << 8) |
                                     ((uint32_t)mesh[k + 2] << 16) | ((uint32_t)mesh[k + 3] << 24));
    vert_list = base + sizeof(mesh);
    for (k = 0; k < jake_verts * 8u; k += 4)
        psx_mod_write_word(vert_list + k, psx_mod_read_word(base + verts + k));
    for (; k < spike_verts * 8u; k += 4)
        psx_mod_write_word(vert_list + k,
                           psx_mod_read_word(psx_mod_read_word(spike_object + 0x10u) + k));
    g_jake_mesh[0] = base;
    g_jake_mesh[1] = base + colours;
    g_jake_mesh[2] = base + polys;
    g_jake_mesh[3] = base + parts;
    g_jake_mesh[4] = vert_list;
    g_jake_mesh_ready = 1u;
}

/*
 * CPU-to-VRAM copies of the face and its palette, as packets linked at the
 * head of the table's far end (the first entry the GPU is given), so they
 * land before anything is drawn with them. Never written to GP0 directly:
 * a VBlank can fall inside one of the game's multi-word GPU commands, and
 * the spliced words desync the GPU (garbled colours, then a crash).
 */
static void jake_link_packet(uint32_t entry, const uint32_t* words, uint32_t count) {
    uint32_t packet, old, k;
    if (g_arena[g_arena_index] == 0u ||
        g_arena_used + 4u + count * 4u > JAKE_ARENA_SIZE)
        return;
    packet = g_arena[g_arena_index] + g_arena_used;
    g_arena_used += 4u + count * 4u;
    old = psx_mod_read_word(entry);
    psx_mod_write_word(packet, (count << 24) | (old & 0x00FFFFFFu));
    for (k = 0; k < count; ++k)
        psx_mod_write_word(packet + 4u + k * 4u, words[k]);
    psx_mod_write_word(entry, (old & 0xFF000000u) | (packet & 0x00FFFFFFu));
}

static void jake_upload_face(uint32_t table) {
    enum { ROWS = 16 };
    const uint32_t far_entry = table + 0x3FFCu;
    uint32_t words[3 + JAKE_VRAM_W * ROWS / 2];
    uint32_t k, row;
    /* Linked at the head, so the last linked is the first drawn: texture
     * cache flush first (drawn after the copies), then the copies. */
    words[0] = 0x01000000u;
    jake_link_packet(far_entry, words, 1u);
    words[0] = 0xA0000000u;
    words[1] = ((uint32_t)JAKE_CLUT_Y << 16) | JAKE_CLUT_X;
    words[2] = (1u << 16) | 16u;
    for (k = 0; k < 16; k += 2)
        words[3 + k / 2] = g_face_clut[k] | ((uint32_t)g_face_clut[k + 1] << 16);
    jake_link_packet(far_entry, words, 3u + 8u);
    for (row = 0; row < JAKE_HUD_ICON_COUNT; ++row) {
        const JakeHudIcon* icon = &JAKE_HUD_ICONS[row];
        if (icon->clut_x == 0u)
            continue;
        words[0] = 0xA0000000u;
        words[1] = ((uint32_t)icon->clut_y << 16) | icon->clut_x;
        words[2] = (1u << 16) | 16u;
        for (k = 0; k < 16; k += 2)
            words[3 + k / 2] = icon->palette[k] | ((uint32_t)icon->palette[k + 1] << 16);
        jake_link_packet(far_entry, words, 3u + 8u);
    }
    for (row = 0; row < JAKE_VRAM_H; row += ROWS) {
        words[0] = 0xA0000000u;
        words[1] = ((uint32_t)(JAKE_VRAM_Y + row) << 16) | JAKE_VRAM_X;
        words[2] = ((uint32_t)ROWS << 16) | JAKE_VRAM_W;
        for (k = 0; k < JAKE_VRAM_W * ROWS; k += 2) {
            const uint32_t at = row * JAKE_VRAM_W + k;
            words[3 + k / 2] = g_face_texels[at] | ((uint32_t)g_face_texels[at + 1] << 16);
        }
        jake_link_packet(far_entry, words, 3u + JAKE_VRAM_W * ROWS / 2u);
    }
}

/* ---- Bones ------------------------------------------------------------- */

static uint32_t jake_part_matrix(uint32_t matrices, int part) {
    return matrices + (uint32_t)(part + 1) * JAKE_MATRIX_SIZE;
}

static void jake_read_affine(uint32_t address, JakeAffine* out) {
    int row, col;
    for (row = 0; row < 3; ++row) {
        for (col = 0; col < 3; ++col)
            out->r[row][col] = (float)(int16_t)psx_mod_read_half(
                address + (uint32_t)(row * 6 + col * 2)) / 4096.0f;
        out->t[row] = (float)(int32_t)psx_mod_read_word(
            address + JAKE_MATRIX_TRANSLATION + (uint32_t)row * 4u);
    }
}

static int16_t jake_fixed(float v) {
    float f = v * 4096.0f;
    if (f > 32767.0f) f = 32767.0f;
    if (f < -32768.0f) f = -32768.0f;
    return (int16_t)(f < 0.0f ? f - 0.5f : f + 0.5f);
}

static void jake_write_affine(uint32_t address, const JakeAffine* a) {
    int row, col;
    for (row = 0; row < 3; ++row) {
        for (col = 0; col < 3; ++col)
            psx_mod_write_half(address + (uint32_t)(row * 6 + col * 2),
                               (uint16_t)jake_fixed(a->r[row][col]));
        psx_mod_write_word(address + JAKE_MATRIX_TRANSLATION + (uint32_t)row * 4u,
                           (uint32_t)(int32_t)(a->t[row] < 0.0f ? a->t[row] - 0.5f
                                                                : a->t[row] + 0.5f));
    }
}

static void jake_multiply(const JakeAffine* a, const JakeAffine* b, JakeAffine* out) {
    JakeAffine m;
    int i, j, k;
    for (i = 0; i < 3; ++i) {
        for (j = 0; j < 3; ++j) {
            m.r[i][j] = 0.0f;
            for (k = 0; k < 3; ++k)
                m.r[i][j] += a->r[i][k] * b->r[k][j];
        }
        m.t[i] = a->t[i];
        for (k = 0; k < 3; ++k)
            m.t[i] += a->r[i][k] * b->t[k];
    }
    *out = m;
}

static int jake_inverse(const JakeAffine* a, JakeAffine* out) {
    const float (*r)[3] = a->r;
    const float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) -
                      r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                      r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    JakeAffine m;
    int i, k;
    if (det > -1e-6f && det < 1e-6f)
        return 0;
    m.r[0][0] = (r[1][1] * r[2][2] - r[1][2] * r[2][1]) / det;
    m.r[0][1] = (r[0][2] * r[2][1] - r[0][1] * r[2][2]) / det;
    m.r[0][2] = (r[0][1] * r[1][2] - r[0][2] * r[1][1]) / det;
    m.r[1][0] = (r[1][2] * r[2][0] - r[1][0] * r[2][2]) / det;
    m.r[1][1] = (r[0][0] * r[2][2] - r[0][2] * r[2][0]) / det;
    m.r[1][2] = (r[0][2] * r[1][0] - r[0][0] * r[1][2]) / det;
    m.r[2][0] = (r[1][0] * r[2][1] - r[1][1] * r[2][0]) / det;
    m.r[2][1] = (r[0][1] * r[2][0] - r[0][0] * r[2][1]) / det;
    m.r[2][2] = (r[0][0] * r[1][1] - r[0][1] * r[1][0]) / det;
    for (i = 0; i < 3; ++i) {
        m.t[i] = 0.0f;
        for (k = 0; k < 3; ++k)
            m.t[i] -= m.r[i][k] * a->t[k];
    }
    *out = m;
    return 1;
}

/*
 * Without Jake in the level to copy from, the tail swings on a spring. Its
 * tip direction u (head-local, unit) is pulled toward the rest direction
 * and pushed back by the head's motion through the world: Spike's velocity
 * and acceleration (actor position, s16 at +0x1C) and the head's own turn,
 * which leaves u behind in the head's frame. Head-to-world is the camera
 * matrix's inverse times the head matrix (both carry the screen's y scale,
 * which cancels), so camera moves don't swing it. Both tail bones turn
 * together about the tail root.
 */
static const uint32_t JAKE_CAMERA_MATRIX = 0x800E78C0u;
static const float JAKE_TAIL_SPRING = 0.12f;     /* per frame^2 */
static const float JAKE_TAIL_DAMPING = 0.22f;    /* per frame */
static const float JAKE_TAIL_DRAG = 0.0022f;     /* per world unit/frame */
static const float JAKE_TAIL_INERTIA = 0.0035f;  /* per world unit/frame^2 */
static const float JAKE_TAIL_MAX_COS = 0.5f;     /* at most 60 degrees off rest */

static struct {
    uint8_t live;
    uint32_t frame;
    float u[3], du[3];          /* tip direction and its rate, head-local */
    float world[3][3];          /* last head-to-world rotation */
    float pos[3], vel[3];       /* Spike, world */
} g_sway;

static void jake_vec_normalize(float v[3]) {
    const float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n > 1e-6f) {
        v[0] /= n, v[1] /= n, v[2] /= n;
    }
}

/* r^T v */
static void jake_mul_transposed(const float r[3][3], const float v[3], float out[3]) {
    int i;
    for (i = 0; i < 3; ++i)
        out[i] = r[0][i] * v[0] + r[1][i] * v[1] + r[2][i] * v[2];
}

/* Rotation taking unit a onto unit b. */
static void jake_rotation_between(const float a[3], const float b[3], float out[3][3]) {
    float k[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                   a[0] * b[1] - a[1] * b[0] };
    const float s = sqrtf(k[0] * k[0] + k[1] * k[1] + k[2] * k[2]);
    const float c = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    const float t = 1.0f - c;
    int i, j;
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j)
            out[i][j] = i == j ? 1.0f : 0.0f;
    if (s < 1e-6f)
        return;
    k[0] /= s, k[1] /= s, k[2] /= s;
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j)
            out[i][j] = t * k[i] * k[j] + (i == j ? c : 0.0f);
    out[1][2] -= s * k[0], out[2][1] += s * k[0];
    out[2][0] -= s * k[1], out[0][2] += s * k[1];
    out[0][1] -= s * k[2], out[1][0] += s * k[2];
}

static void jake_sway_tail(const JakeAffine* head, JakeAffine out[2]) {
    JakeAffine camera, inverse, world;
    float rest[3], pos[3], vel[3], acc[3], push[3], push_local[3], q[3][3];
    int i, j, a, b, c;

    for (i = 0; i < 3; ++i)
        rest[i] = JAKE_TAIL_REST[1].t[i] - JAKE_TAIL_REST[0].t[i];
    jake_vec_normalize(rest);
    out[0] = JAKE_TAIL_REST[0], out[1] = JAKE_TAIL_REST[1];
    jake_read_affine(JAKE_CAMERA_MATRIX, &camera);
    if (!jake_inverse(&camera, &inverse)) {
        g_sway.live = 0u;
        return;
    }
    jake_multiply(&inverse, head, &world);
    for (i = 0; i < 3; ++i) {      /* columns back to unit length */
        float col[3] = { world.r[0][i], world.r[1][i], world.r[2][i] };
        jake_vec_normalize(col);
        world.r[0][i] = col[0], world.r[1][i] = col[1], world.r[2][i] = col[2];
    }
    for (i = 0; i < 3; ++i) {
        pos[i] = (float)(int16_t)psx_mod_read_half(JAKE_SPIKE_OBJECT + 0x1Cu + (uint32_t)i * 2u);
        vel[i] = pos[i] - g_sway.pos[i];
    }
    /* Level load, respawn, a long pause: start again from rest. */
    if (!g_sway.live || g_frame - g_sway.frame > 3u ||
        fabsf(vel[0]) + fabsf(vel[1]) + fabsf(vel[2]) > 400.0f) {
        g_sway.live = 1u;
        for (i = 0; i < 3; ++i) {
            g_sway.u[i] = rest[i], g_sway.du[i] = 0.0f, g_sway.vel[i] = 0.0f;
            vel[i] = 0.0f;
        }
    } else {
        /* The head turned under the tail: carry u by the opposite turn. */
        float w_u[3], w_du[3];
        for (i = 0; i < 3; ++i) {
            w_u[i] = 0.0f, w_du[i] = 0.0f;
            for (j = 0; j < 3; ++j) {
                w_u[i] += g_sway.world[i][j] * g_sway.u[j];
                w_du[i] += g_sway.world[i][j] * g_sway.du[j];
            }
        }
        jake_mul_transposed(world.r, w_u, g_sway.u);
        jake_mul_transposed(world.r, w_du, g_sway.du);
    }
    for (i = 0; i < 3; ++i) {
        acc[i] = vel[i] - g_sway.vel[i];
        push[i] = -(JAKE_TAIL_DRAG * vel[i] + JAKE_TAIL_INERTIA * acc[i]);
        g_sway.pos[i] = pos[i], g_sway.vel[i] = vel[i];
    }
    jake_mul_transposed(world.r, push, push_local);
    for (i = 0; i < 3; ++i) {
        g_sway.du[i] += JAKE_TAIL_SPRING * (rest[i] - g_sway.u[i]) -
                        JAKE_TAIL_DAMPING * g_sway.du[i] + push_local[i];
        g_sway.u[i] += g_sway.du[i];
    }
    jake_vec_normalize(g_sway.u);
    if (g_sway.u[0] * rest[0] + g_sway.u[1] * rest[1] + g_sway.u[2] * rest[2] <
        JAKE_TAIL_MAX_COS) {       /* keep it within reach of rest */
        for (i = 0; i < 3; ++i) {
            g_sway.u[i] = rest[i] + (g_sway.u[i] - rest[i]) * 0.9f;
            g_sway.du[i] *= 0.5f;
        }
        jake_vec_normalize(g_sway.u);
    }
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j)
            g_sway.world[i][j] = world.r[i][j];
    g_sway.frame = g_frame;

    jake_rotation_between(rest, g_sway.u, q);
    for (j = 0; j < 2; ++j) {
        const JakeAffine* r = &JAKE_TAIL_REST[j];
        float off[3];
        for (a = 0; a < 3; ++a)
            off[a] = r->t[a] - JAKE_TAIL_REST[0].t[a];
        for (a = 0; a < 3; ++a) {
            for (b = 0; b < 3; ++b) {
                out[j].r[a][b] = 0.0f;
                for (c = 0; c < 3; ++c)
                    out[j].r[a][b] += q[a][c] * r->r[c][b];
            }
            out[j].t[a] = JAKE_TAIL_REST[0].t[a];
            for (c = 0; c < 3; ++c)
                out[j].t[a] += q[a][c] * off[c];
        }
    }
}

/* Spike's skeleton has no tail: pose Jake's on Spike's head. */
static void jake_pose_tail(uint32_t matrices) {
    JakeAffine head, tail, swayed[2];
    /* Spike draws before Jake, so the live pose is a frame old. */
    const int live = g_have_tail && g_frame - g_tail_frame <= 2u;
    int k;
    jake_read_affine(jake_part_matrix(matrices, JAKE_HEAD_PART), &head);
    if (!live)
        jake_sway_tail(&head, swayed);
    for (k = 0; k < 2; ++k) {
        jake_multiply(&head, live ? &g_tail_rel[k] : &swayed[k], &tail);
        jake_write_affine(jake_part_matrix(matrices, JAKE_TAIL_PART + k), &tail);
    }
}

static void jake_capture_tail(uint32_t matrices) {
    JakeAffine head, inv, tail;
    int k;
    jake_read_affine(jake_part_matrix(matrices, JAKE_HEAD_PART), &head);
    if (!jake_inverse(&head, &inv))
        return;
    for (k = 0; k < 2; ++k) {
        jake_read_affine(jake_part_matrix(matrices, JAKE_TAIL_PART + k), &tail);
        jake_multiply(&inv, &tail, &g_tail_rel[k]);
    }
    g_have_tail = 1u;
    g_tail_frame = g_frame;
}

/* ---- Hooks ------------------------------------------------------------- */

static uint16_t jake_part_count(uint32_t header) {
    return psx_mod_read_half(header + 4u);
}

/* Jake's mesh as a level loads it: 18 parts, the tail's two parts chained
 * off the head (part table parent field is the parent's index + 1). */
static int jake_is_jake_mesh(uint32_t header, uint32_t parts) {
    if (header < 0x80000000u || header >= 0x80200000u ||
        jake_part_count(header) != JAKE_JAKE_PARTS)
        return 0;
    return psx_mod_read_half(parts + JAKE_TAIL_PART * JAKE_PART_ENTRY_SIZE + 10u) ==
               JAKE_HEAD_PART + 1u &&
           psx_mod_read_half(parts + (JAKE_TAIL_PART + 1) * JAKE_PART_ENTRY_SIZE + 10u) ==
               JAKE_TAIL_PART + 1u;
}

static void jake_read_mesh(uint32_t object, uint32_t words[JAKE_MESH_WORDS]) {
    int i;
    for (i = 0; i < JAKE_MESH_WORDS; ++i)
        words[i] = psx_mod_read_word(object + (uint32_t)i * 4u);
}

static int jake_is_swapped(uint32_t object) {
    int i;
    for (i = 0; i < g_swapped_count; ++i)
        if (g_swapped[i] == object)
            return 1;
    return 0;
}

static void jake_swap_in(uint32_t object, const uint32_t mesh[JAKE_MESH_WORDS]) {
    int i;
    if (g_swapped_count >= 8)
        return;
    g_swapped[g_swapped_count] = object;
    jake_read_mesh(object, g_swapped_words[g_swapped_count]);
    ++g_swapped_count;
    for (i = 0; i < JAKE_MESH_WORDS; ++i)
        psx_mod_write_word(object + (uint32_t)i * 4u, mesh[i]);
}

static void jake_transform_entry(struct CPUState* cpu, uint32_t address) {
    const uint32_t object = cpu->gpr[4];
    uint32_t header, matrices;
    (void)address;

    if (!g_enabled || jake_is_swapped(object))
        return;
    header = psx_mod_read_word(object);
    matrices = psx_mod_read_word(object + 0x14u);
    if (object == JAKE_SPIKE_OBJECT) {
        if (jake_part_count(header) != JAKE_SPIKE_PARTS)
            return;
        jake_prepare_mesh(object);
        if (!g_jake_mesh_ready)
            return;
        jake_read_mesh(object, g_spike_mesh);
        g_have_spike = 1u;
        jake_pose_tail(matrices);
        jake_swap_in(object, g_jake_mesh);
        return;
    }
    /* Jake himself (his race, cutscenes): he wears Spike. */
    if (!jake_is_jake_mesh(header, psx_mod_read_word(object + 0x0Cu)))
        return;
    jake_capture_tail(matrices);
    if (g_have_spike)
        jake_swap_in(object, g_spike_mesh);
}

/* Point Spike's HUD sprites (texture-page word, then a 0x64..0x67 sprite:
 * colour, xy, clut<<16 | uv, size) at the recoloured palettes. The HUD list
 * hangs off table + 0x4000 and is drawn after the main table. */
static void jake_recolour_hud(uint32_t table) {
    uint32_t at = psx_mod_read_word(table + 0x4000u) & 0x00FFFFFFu;
    int packets;
    for (packets = 0; packets < 512 && at != 0x00FFFFFFu && at != 0u; ++packets) {
        const uint32_t addr = 0x80000000u | at;
        const uint32_t tag = psx_mod_read_word(addr);
        const uint32_t count = tag >> 24;
        uint32_t k;
        for (k = 1; k + 3 <= count; ++k) {
            const uint32_t op = psx_mod_read_word(addr + 4u + k * 4u) >> 24;
            const uint32_t page = psx_mod_read_word(addr + (k - 1u) * 4u + 4u);
            uint32_t uv_word;
            int i;
            if (op < 0x64u || op > 0x67u || (page >> 24) != 0xE1u)
                continue;
            uv_word = psx_mod_read_word(addr + 4u + (k + 2u) * 4u);
            for (i = 0; i < JAKE_HUD_ICON_COUNT; ++i) {
                const JakeHudIcon* icon = &JAKE_HUD_ICONS[i];
                if ((page & 0x1FFu) == icon->tpage && (uv_word & 0xFFFFu) == icon->uv &&
                    (uv_word >> 16) == icon->stock_clut) {
                    const uint32_t clut = icon->clut_x != 0u
                        ? ((uint32_t)icon->clut_y << 6) | (icon->clut_x >> 4u)
                        : icon->stock_clut;
                    psx_mod_write_word(addr + 4u + (k + 2u) * 4u, (clut << 16) | icon->new_uv);
                    break;   /* the swapped uv must not match the other entry */
                }
            }
        }
        at = tag & 0x00FFFFFFu;
    }
}

/* The frame's drawing is queued: give every swapped object its own mesh
 * back before anything animates it again. */
static void jake_draw_ot_entry(struct CPUState* cpu, uint32_t address) {
    static uint32_t uploads;
    uint32_t index;
    int i, k;
    (void)address;
    /* Keep the face in VRAM (level loads may write over that strip). */
    for (index = 0u; index < 2u; ++index) {
        const uint32_t table = JAKE_ORDERING_TABLE + JAKE_ORDERING_TABLE_STRIDE * index;
        if (g_enabled && g_jake_mesh_ready && cpu->gpr[4] == table + 0x3FFCu &&
            uploads++ % JAKE_UPLOAD_PERIOD == 0u) {
            if (g_arena[index] == 0u)
                g_arena[index] = psx_mod_alloc_gpu_dma_memory(JAKE_ARENA_SIZE, 16u);
            g_arena_index = (int)index;
            g_arena_used = 0u;
            jake_upload_face(table);
        }
        if (g_enabled && g_jake_mesh_ready && cpu->gpr[4] == table + 0x3FFCu)
            jake_recolour_hud(table);
    }
    for (i = 0; i < g_swapped_count; ++i)
        for (k = 0; k < JAKE_MESH_WORDS; ++k)
            psx_mod_write_word(g_swapped[i] + (uint32_t)k * 4u, g_swapped_words[i][k]);
    g_swapped_count = 0;
    ++g_frame;
}

static void jake_load_voice(void) {
    static uint8_t disc[JAKE_VOICE_SIZE];
    char path[1024];
    FILE* f;
    if (!jake_disc_path(path, sizeof(path)) || !(f = fopen(path, "rb")))
        return;
    if (jake_read_disc(f, JAKE_DISC_VOICE, disc, JAKE_VOICE_SIZE) &&
        jake_read_disc(f, SPIKE_DISC_YELL, g_spike_yell_head, sizeof(g_spike_yell_head)) &&
        disc[17] == 0x04u && g_spike_yell_head[17] == 0x04u)
        jake_build_voice(disc);
    else
        fprintf(stderr, "play-as-jake: Jake's voice was not found on this disc\n");
    fclose(f);
}

static void jake_activate(void) {
    g_enabled = 1u;
    jake_load_disc();
    jake_load_voice();
}

PSX_MOD_CONSTRUCTOR(ape_register_play_as_jake_plugin) {
    (void)psx_mod_register_activation_plugin("ape.characters.play-as-jake", jake_activate);
    (void)psx_mod_register_vblank_plugin("ape.characters.play-as-jake", jake_vblank);
    (void)psx_mod_register_function_entry_plugin(
        "ape.characters.play-as-jake", JAKE_TRANSFORM_FUNCTION, jake_transform_entry);
    (void)psx_mod_register_function_entry_plugin(
        "ape.characters.play-as-jake", JAKE_DRAW_OT_FUNCTION, jake_draw_ot_entry);
}
