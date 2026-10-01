#include "cpu_state.h"
#include "mod_plugins.h"

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
static const uint32_t JAKE_GP0 = 0x1F801810u;

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

/* CPU-to-VRAM copies of the face and its palette. */
static void jake_upload_face(void) {
    uint32_t k;
    psx_mod_write_word(JAKE_GP0, 0x01000000u);   /* clear texture cache */
    psx_mod_write_word(JAKE_GP0, 0xA0000000u);
    psx_mod_write_word(JAKE_GP0, ((uint32_t)JAKE_CLUT_Y << 16) | JAKE_CLUT_X);
    psx_mod_write_word(JAKE_GP0, (1u << 16) | 16u);
    for (k = 0; k < 16; k += 2)
        psx_mod_write_word(JAKE_GP0, g_face_clut[k] | ((uint32_t)g_face_clut[k + 1] << 16));
    psx_mod_write_word(JAKE_GP0, 0xA0000000u);
    psx_mod_write_word(JAKE_GP0, ((uint32_t)JAKE_VRAM_Y << 16) | JAKE_VRAM_X);
    psx_mod_write_word(JAKE_GP0, ((uint32_t)JAKE_VRAM_H << 16) | JAKE_VRAM_W);
    for (k = 0; k < JAKE_VRAM_W * JAKE_VRAM_H; k += 2)
        psx_mod_write_word(JAKE_GP0, g_face_texels[k] | ((uint32_t)g_face_texels[k + 1] << 16));
    psx_mod_write_word(JAKE_GP0, 0x01000000u);
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

/* Spike's skeleton has no tail: pose Jake's on Spike's head. */
static void jake_pose_tail(uint32_t matrices) {
    JakeAffine head, tail;
    int k;
    jake_read_affine(jake_part_matrix(matrices, JAKE_HEAD_PART), &head);
    for (k = 0; k < 2; ++k) {
        /* Spike draws before Jake, so the live pose is a frame old. */
        if (g_have_tail && g_frame - g_tail_frame <= 2u)
            jake_multiply(&head, &g_tail_rel[k], &tail);
        else
            jake_multiply(&head, &JAKE_TAIL_REST[k], &tail);
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

/* The frame's drawing is queued: give every swapped object its own mesh
 * back before anything animates it again. */
static void jake_draw_ot_entry(struct CPUState* cpu, uint32_t address) {
    int i, k;
    (void)cpu;
    (void)address;
    for (i = 0; i < g_swapped_count; ++i)
        for (k = 0; k < JAKE_MESH_WORDS; ++k)
            psx_mod_write_word(g_swapped[i] + (uint32_t)k * 4u, g_swapped_words[i][k]);
    g_swapped_count = 0;
    ++g_frame;
}

/* Keep the face in VRAM (level loads may write over that strip). */
static void jake_vblank(void) {
    static uint32_t frames;
    if (g_jake_mesh_ready && frames++ % JAKE_UPLOAD_PERIOD == 0u)
        jake_upload_face();
}

static void jake_activate(void) {
    g_enabled = 1u;
    jake_load_disc();
}

PSX_MOD_CONSTRUCTOR(ape_register_play_as_jake_plugin) {
    (void)psx_mod_register_activation_plugin("ape.characters.play-as-jake", jake_activate);
    (void)psx_mod_register_vblank_plugin("ape.characters.play-as-jake", jake_vblank);
    (void)psx_mod_register_function_entry_plugin(
        "ape.characters.play-as-jake", JAKE_TRANSFORM_FUNCTION, jake_transform_entry);
    (void)psx_mod_register_function_entry_plugin(
        "ape.characters.play-as-jake", JAKE_DRAW_OT_FUNCTION, jake_draw_ot_entry);
}
