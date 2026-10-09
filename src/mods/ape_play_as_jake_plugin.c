#include "cpu_state.h"
#include "mod_plugins.h"
#include "cdrom.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
enum { JAKE_ARENA_SIZE = 0x6000 };
static uint32_t g_arena[2], g_arena_used;
static int g_arena_index;

/* Disc offsets below are user-data offsets (2048-byte sectors) in KKIIDDZZ.DAT. */

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
 *  - health flame behind the cookies (page 0x1A, palette 0x7A2B): its
 *    yellow-to-red becomes turquoise-to-purple, shade for shade (same
 *    brightness steps, same semi-transparency bits). Matched at any uv.
 * The palettes the game loaded are left alone, so nothing else that shares
 * them changes.
 */
enum { JAKE_ANY_UV = 0xFFFFu };     /* match any uv and keep it */
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
    { 0x1Au, JAKE_ANY_UV, 0x7A2Bu, JAKE_ANY_UV, 992u, 254u, {
      0x0000, 0xA867, 0x544E, 0x6030, 0x6C32, 0x78B4, 0x7D53, 0x7DD0,
      0x7E2D, 0x7E89, 0x7AE4, 0x7723, 0x6F64, 0x73CE, 0x77F3, 0x7BF9 } },
};
enum { JAKE_HUD_ICON_COUNT = sizeof(JAKE_HUD_ICONS) / sizeof(JAKE_HUD_ICONS[0]) };

/*
 * Redrawn icon art (4-bit, the recoloured palettes' indices), written over
 * Spike's texels in place: the face (Jake's hair with its two spikes, no
 * headband; page 0x17 texels at VRAM (472,384), 18 wide plus 2 transparent
 * to fill the last halfword) and the lives shirt (sleeveless, no stripes;
 * page 0x4F at (1008,96), 32x32). Kept uploaded with the face texture.
 */
static const uint16_t JAKE_FACE_ICON_TEXELS[80] = {
    0x0000, 0x0000, 0x0300, 0x0000, 0x0000,
    0x0000, 0x0000, 0x3430, 0x0000, 0x0000,
    0x3000, 0x0000, 0x3543, 0x0000, 0x0000,
    0x4300, 0x3003, 0x3554, 0x0000, 0x0000,
    0x4300, 0x4335, 0x5E55, 0x0003, 0x0000,
    0x5430, 0x5555, 0xEE55, 0x0345, 0x0000,
    0xE543, 0x555E, 0xE555, 0x3455, 0x0000,
    0xE543, 0x5555, 0x5555, 0x45E5, 0x0003,
    0xE543, 0x5455, 0x5455, 0x45E5, 0x0003,
    0x4543, 0xA945, 0xA9B4, 0x4549, 0x0003,
    0x4430, 0x459A, 0x99AA, 0x3341, 0x0000,
    0x9330, 0xF49B, 0x2772, 0x029D, 0x0000,
    0x2000, 0xFDB9, 0x2BB2, 0x029D, 0x0000,
    0x0000, 0xBBA2, 0x7BBB, 0x002A, 0x0000,
    0x0000, 0xA920, 0xA777, 0x0002, 0x0000,
    0x0000, 0x2200, 0x2222, 0x0000, 0x0000
};
static const uint16_t JAKE_SHIRT_ICON_TEXELS[256] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0xCC00, 0x000C, 0xC000, 0x00CC, 0x0000, 0x0000,
    0x0000, 0x0000, 0x66C0, 0x00C5, 0x4C00, 0x0C44, 0x0000, 0x0000,
    0x0000, 0x0000, 0x66C0, 0x00C5, 0x4C00, 0x0C44, 0x0000, 0x0000,
    0x0000, 0x0000, 0x666C, 0x00C5, 0x4C00, 0xC444, 0x0000, 0x0000,
    0x0000, 0x0000, 0x666C, 0x0C55, 0x44C0, 0xC444, 0x0000, 0x0000,
    0x0000, 0xC000, 0x6669, 0x0C55, 0x44C0, 0x3444, 0x000C, 0x0000,
    0x0000, 0xC000, 0x6669, 0xC555, 0x444C, 0x3444, 0x000C, 0x0000,
    0x0000, 0x9C00, 0x6669, 0x5555, 0x4445, 0x3444, 0x00C3, 0x0000,
    0x0000, 0x99C0, 0x6669, 0x5555, 0x4445, 0x3444, 0x0C33, 0x0000,
    0x0000, 0x99C0, 0x6669, 0x5555, 0x4445, 0x3444, 0x0C33, 0x0000,
    0x0000, 0x99C0, 0x6669, 0x5555, 0x4445, 0x3444, 0x0C33, 0x0000,
    0x0000, 0x99C0, 0x6669, 0x5555, 0x4445, 0x3444, 0x0C33, 0x0000,
    0x0000, 0x99C0, 0x6669, 0x5555, 0x4445, 0x3444, 0x0C33, 0x0000,
    0x0000, 0x99C0, 0x6669, 0x5555, 0x4445, 0x3444, 0x0C33, 0x0000,
    0x0000, 0x99C0, 0x6669, 0x5555, 0x4445, 0x3444, 0x0C33, 0x0000,
    0x0000, 0x99C0, 0x6669, 0x5555, 0x4445, 0x3444, 0x0C33, 0x0000,
    0x0000, 0x99C0, 0x6669, 0x5555, 0x4445, 0x3444, 0x0C33, 0x0000,
    0x0000, 0x99C0, 0x6669, 0x5555, 0x4445, 0x3444, 0x0C33, 0x0000,
    0x0000, 0x99C0, 0x6669, 0x5555, 0x4445, 0x3444, 0x0C33, 0x0000,
    0x0000, 0x99C0, 0x6669, 0x5555, 0x4445, 0x3444, 0x0C33, 0x0000,
    0x0000, 0x99C0, 0x6669, 0x5555, 0x4445, 0x3444, 0x0C33, 0x0000,
    0x0000, 0x33C0, 0x3333, 0x3333, 0x2223, 0x2222, 0x0C22, 0x0000,
    0x0000, 0x22C0, 0x2222, 0x2222, 0x2222, 0xD222, 0x0CDD, 0x0000,
    0x0000, 0x22C0, 0x2222, 0x2222, 0x2222, 0xD222, 0x0CDD, 0x0000,
    0x0000, 0x2C00, 0x2222, 0x2222, 0x2222, 0xD222, 0x00CD, 0x0000,
    0x0000, 0xC000, 0xCCCC, 0xCCCC, 0xCCCC, 0xCCCC, 0x000C, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
};

static const struct {
    uint16_t x, y, w_halfwords, h;
    const uint16_t* texels;
} JAKE_HUD_ART[] = {
    { 472u, 384u, 5u, 16u, JAKE_FACE_ICON_TEXELS },
    { 1008u, 96u, 8u, 32u, JAKE_SHIRT_ICON_TEXELS },
};
enum { JAKE_HUD_ART_COUNT = sizeof(JAKE_HUD_ART) / sizeof(JAKE_HUD_ART[0]) };

/*
 * The extra-life pickup is Spike's red shirt: a level model (entry of four
 * words: header, colours, polys, verts) in the table around 0x800F4C80,
 * 20 verts and 40 textured polys (28 bytes each, palette at +14) on a red
 * patch with a white stripe. Made Jake's like the lives icon: the polys use
 * a navy palette uploaded to (960,254), the stripe's whites the same navy,
 * and the sleeves are folded away (each sleeve vertex moved onto the
 * shoulder vertex it hangs from, so their polys draw nothing). Patched in
 * place in the level data; the next level load brings back the stock copy.
 */
enum {
    JAKE_LIFE_VERTS = 20, JAKE_LIFE_POLYS = 40, JAKE_LIFE_COLOURS = 13,
    JAKE_LIFE_POLY_SIZE = 28,
    JAKE_LIFE_CLUT_X = 960, JAKE_LIFE_CLUT_Y = 254,
    JAKE_LIFE_SCAN_PERIOD = 30
};
static const uint32_t JAKE_LIFE_TABLE = 0x800F4000u, JAKE_LIFE_TABLE_END = 0x800F5000u;
static const uint16_t JAKE_LIFE_PALETTE[16] = {
    0xA4A5, 0xB4E7, 0xB4E7, 0xBD08, 0xB4E7, 0xA8A5, 0xB8E7, 0xB4E7,
    0xB0C6, 0xA8A5, 0xB4E7, 0xB4E7, 0xBD08, 0xB8E7, 0xA4A5, 0xA8A5
};
/* Sleeve vertex -> shoulder vertex. */
static const uint8_t JAKE_LIFE_FOLD[8][2] = {
    { 2, 1 }, { 14, 13 }, { 3, 4 }, { 15, 16 },
    { 11, 0 }, { 5, 12 }, { 10, 9 }, { 17, 8 },
};
/* Untouched verts that identify the model (index, x, y, z). */
static const int16_t JAKE_LIFE_KEY[3][4] = {
    { 0, 8, -51, 35 }, { 18, 28, 48, -39 }, { 19, -35, 49, 24 },
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
static uint16_t g_face_clut[16];
static uint16_t g_face_texels[JAKE_VRAM_W * JAKE_VRAM_H];
static uint8_t g_disc_ok;

/*
 * Two looks: every boot starts as NORMAL, and L1 on the title screen
 * switches for that session (nothing is saved). EVIL (shown as
 * BRAINWASHED) is Jake as he races you (the mesh above, red
 * slanted eyes, purple hair on the HUD), NORMAL is Jake before Specter gets
 * him, as in the opening cutscene (blue hair, big brown eyes). The normal
 * mesh is the intro's (USA disc 0x008AE9D0; its face quads' uvs are already
 * relative to the 64x64 face block, so the same retarget works), and its
 * face is that cutscene's base face block (palette + packed image, same
 * format). Both faces use the same VRAM slot and palette position.
 */
enum { JAKE_EVIL, JAKE_NORMAL, JAKE_LOOKS };
/* Meshes are in KKIIDDZZ.DAT; face_file holds the palette + packed image. All
 * offsets are within their file (USA disc user-data offsets 0x033E23EC /
 * 0x033BDCB0 evil, 0x008AE9D0 / 0x06625764 normal). */
static const char JAKE_DAT_FILE[] = "KKIIDDZZ.DAT";
static const struct {
    uint32_t mesh, mesh_size;
    const char* face_file;
    uint32_t clut, image, image_size;
    uint8_t signature[12];
} JAKE_LOOK_DISC[JAKE_LOOKS] = {
    { 0x033303ECu, 0x1E38u, JAKE_DAT_FILE, 0x0330BCB0u, 0x0330BCB0u + 0x2Cu, 0x1C4u - 12u,
      { 0x20, 0x00, 0x36, 0x01, 0x12, 0x00, 0x25, 0x01, 0x24, 0x00, 0x00, 0x00 } },
    { 0x007FC9D0u, 0x1D8Cu, "DEMO/OP.ALL", 0x00000F64u, 0x00000F90u, 0x2CCu - 12u,
      { 0x22, 0x00, 0x27, 0x01, 0x12, 0x00, 0x0F, 0x01, 0x67, 0x00, 0x00, 0x00 } },
};
static uint8_t g_look_mesh[JAKE_LOOKS][0x1E38];
static uint16_t g_look_clut[JAKE_LOOKS][16];
static uint16_t g_look_texels[JAKE_LOOKS][JAKE_VRAM_W * JAKE_VRAM_H];
static uint32_t g_look_words[JAKE_LOOKS][5];
static uint8_t g_look_ok[JAKE_LOOKS], g_look_built[JAKE_LOOKS];
static int g_look = JAKE_NORMAL;   /* every boot; L1 on the title switches */
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
static uint32_t g_tail_frame, g_frame, g_level_spike_frame = 0x80000000u;
static uint8_t g_have_tail;

/* ---- Disc -------------------------------------------------------------- */

static void jake_unpack_face(const uint8_t* packed, uint32_t size, uint16_t* texels) {
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
        texels[k] = (uint16_t)(pixels[k * 4] | (pixels[k * 4 + 1] << 4) |
                                      (pixels[k * 4 + 2] << 8) | (pixels[k * 4 + 3] << 12));
}

/*
 * A whole file from the disc the game has mounted, read through the runtime,
 * so it works for any image the game itself accepts (.cue/.bin, .chd, ...)
 * however the game was launched. The runtime only hands out whole files, so
 * the 56 MB KKIIDDZZ.DAT is read once and freed after the pieces are copied.
 */
typedef struct {
    uint8_t* data;
    uint32_t size;
} JakeDiscFile;

static JakeDiscFile jake_disc_file(const char* name) {
    JakeDiscFile file = { NULL, 0u };
    uint32_t size = 0u;
    if (!psx_mod_read_disc_file(name, NULL, 0u, &size) || size == 0u ||
        !(file.data = (uint8_t*)malloc(size)))
        return file;
    if (!psx_mod_read_disc_file(name, file.data, size, &file.size)) {
        free(file.data);
        file.data = NULL;
        file.size = 0u;
    }
    return file;
}

static int jake_disc_piece(const JakeDiscFile* file, uint32_t offset, uint8_t* out, uint32_t size) {
    if (!file->data || offset > file->size || size > file->size - offset)
        return 0;
    memcpy(out, file->data + offset, size);
    return 1;
}

static void jake_load_disc(const JakeDiscFile* dat) {
    uint8_t clut[32], packed[0x400];
    int k, look;
    for (look = 0; look < JAKE_LOOKS; ++look) {
        const uint32_t size = JAKE_LOOK_DISC[look].mesh_size;
        JakeDiscFile face = { dat->data, dat->size };
        if (strcmp(JAKE_LOOK_DISC[look].face_file, JAKE_DAT_FILE) != 0)
            face = jake_disc_file(JAKE_LOOK_DISC[look].face_file);
        if (jake_disc_piece(dat, JAKE_LOOK_DISC[look].mesh, g_look_mesh[look], size) &&
            jake_disc_piece(&face, JAKE_LOOK_DISC[look].clut, clut, sizeof(clut)) &&
            jake_disc_piece(&face, JAKE_LOOK_DISC[look].image, packed, JAKE_LOOK_DISC[look].image_size) &&
            memcmp(g_look_mesh[look], JAKE_LOOK_DISC[look].signature, 12) == 0) {
            for (k = 0; k < 16; ++k)
                g_look_clut[look][k] = (uint16_t)(clut[k * 2] | (clut[k * 2 + 1] << 8));
            jake_unpack_face(packed, JAKE_LOOK_DISC[look].image_size, g_look_texels[look]);
            g_look_ok[look] = 1u;
        } else {
            fprintf(stderr, "play-as-jake: Jake's %s look was not found on this disc\n",
                    look == JAKE_EVIL ? "evil" : "normal");
        }
        if (face.data != dat->data)
            free(face.data);
    }
    g_disc_ok = g_look_ok[JAKE_EVIL];
    if (!g_look_ok[g_look])
        g_look = JAKE_EVIL;
}

/* ---- Voice ------------------------------------------------------------- */

/*
 * Spike's yells become Jake's jump yell (the one he gives leaping into the
 * water in his race): the double jump's (SPU sample 0x38F00, 0xC30 bytes)
 * and the Stun Club swing's (0x39B40, 0x8D0 bytes), both keyed at pitch
 * 0x4B8 (~13 kHz). Jake's is in his race's sound bank only (disc user
 * offset 0x2E9948, 0xDE0 bytes, pitch 0x5D2, ~16 kHz). At start it is read
 * from the disc, decoded, resampled to Spike's playback rate (so he keeps
 * his own pitch) and re-encoded for each slot, cut short with a fade where
 * the slot is smaller (its tail is the quiet end of the yell). Every half
 * second, a slot that holds Spike's yell (a level load puts it back) gets
 * Jake's written over it through the SPU's manual transfer port; anything
 * else there is left alone.
 *
 * Spike's hurt cry (0x3A420) becomes Jake's "ouch" from the cutscene after
 * the Jake Attack 3 race: the second word of his "ow, ouch" as he gets up,
 * streamed as XA audio (file 1, channel 1, stereo 37.8 kHz, every 4th sector)
 * from LBA 143876, 18522 samples. It is decoded from the disc, mixed to mono
 * and resampled to the slot's rate (its very end is cut to fit).
 *
 * Spike's other lines Jake doesn't say are silenced the same way (a silent
 * block that ends the voice at once): the club spin's "hyaa" (0x38560, which
 * also plays on a catch) and "Gotcha!" (0x36B30).
 * These all sit in one bank, disc offset = SPU address + 0x120FF0.
 */
uint32_t spu_ram_peek(uint32_t addr, uint8_t* out, uint32_t len);

/* Offsets within KKIIDDZZ.DAT (USA disc user-data offset 0x002E9948). */
static const uint32_t JAKE_DISC_VOICE = 0x00237948u;
enum {
    JAKE_VOICE_SIZE = 0xDE0,
    JAKE_VOICE_PITCH = 0x5D2,
    SPIKE_YELL_PITCH = 0x4B8,
    SPIKE_YELL_MAX_SIZE = 0xD00,    /* largest slot we write (the hurt cry) */
    JAKE_VOICE_FADE = 320,          /* samples, ~25 ms */
    JAKE_VOICE_CHECK_PERIOD = 30
};
static const uint32_t SPU_TRANSFER_ADDRESS = 0x1F801DA6u;
static const uint32_t SPU_TRANSFER_FIFO = 0x1F801DA8u;

static const uint32_t JAKE_OUCH_LBA = 143876u;
static const char JAKE_OUCH_FILE[] = "DEMO/A_RAI_D2.STR";
enum {
    JAKE_OUCH_SAMPLES = 18522,      /* at 37.8 kHz */
    JAKE_OUCH_WARMUP = 4,           /* channel sectors decoded first, to settle the filter */
    JAKE_OUCH_SECTORS = 10,
    JAKE_OUCH_STRIDE = 4,           /* LBAs between the channel's sectors */
    JAKE_OUCH_FADE_IN = 454,        /* 12 ms */
    JAKE_OUCH_FADE_OUT = 1134,      /* 30 ms */
    XA_SECTOR_SAMPLES = 2016        /* per stereo channel */
};

enum { JAKE_YELL, JAKE_SILENT, JAKE_OUCH };

static struct {
    uint32_t spu, size, disc;
    uint8_t source;                 /* JAKE_YELL / JAKE_SILENT / JAKE_OUCH */
    uint8_t head[64];               /* Spike's first blocks, to recognise it */
    uint8_t jake[SPIKE_YELL_MAX_SIZE];
    uint32_t jake_size;
} g_yells[] = {
    { 0x38F00u, 0xC30u, 0x000A7EF0u, JAKE_YELL },    /* double jump */
    { 0x39B40u, 0x8D0u, 0x000A8B30u, JAKE_YELL },    /* Stun Club swing */
    { 0x38560u, 0x990u, 0x000A7550u, JAKE_SILENT },  /* Stun Club spin, catch */
    { 0x36B30u, 0x1A20u, 0x000A5B20u, JAKE_SILENT }, /* "Gotcha!" */
    { 0x3A420u, 0xD00u, 0x000A9410u, JAKE_OUCH },    /* hurt */
};
enum { JAKE_YELL_COUNT = sizeof(g_yells) / sizeof(g_yells[0]) };

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

/* Fills a slot: its silent lead-in block, then `blocks` blocks of pcm. */
static void jake_encode_slot(int y, const int16_t* pcm, uint32_t blocks) {
    uint32_t b;
    int s1 = 0, s2 = 0;
    memset(g_yells[y].jake, 0, 16);   /* the silent lead-in block, as stock */
    for (b = 0; b < blocks; ++b)
        jake_adpcm_block(pcm + b * 28u, &s1, &s2,
                         (uint8_t)(b == 0 ? 0x04u : b + 1u == blocks ? 0x01u : 0x00u),
                         g_yells[y].jake + 16u * (b + 1u));
    g_yells[y].jake_size = 16u * (blocks + 1u);
}

/*
 * The "ouch" (37.8 kHz mono, faded in) resampled to the hurt cry's playback
 * rate (pitch 0x4B8, ~13 kHz) so it plays at its natural speed and pitch -
 * squeezing all of it in (10% faster) sounded too high. The slot holds
 * 0.45 s of the 0.49 s, so the last ~40 ms of the "ch" are cut with a fade.
 * Each output sample is the mean of the source samples it covers, which also
 * keeps the downsampling from aliasing. Peak-normalised to Spike's level.
 */
enum { JAKE_OUCH_CUT_FADE = 390 };  /* 30 ms at the slot rate */

static void jake_build_ouch(int y, const int16_t* ouch) {
    static int16_t out[SPIKE_YELL_MAX_SIZE / 16 * 28];
    const uint32_t blocks = g_yells[y].size / 16u - 1u, n = blocks * 28u;
    const double step = 37800.0 * 4096.0 / (44100.0 * SPIKE_YELL_PITCH);
    static double sums[SPIKE_YELL_MAX_SIZE / 16 * 28];
    double peak = 1.0;
    uint32_t i;
    for (i = 0; i < n; ++i) {
        uint32_t k = (uint32_t)(i * step), e = (uint32_t)((i + 1u) * step), c = 0;
        double sum = 0.0;
        for (; k < e && k < JAKE_OUCH_SAMPLES; ++k, ++c)
            sum += ouch[k];
        sums[i] = c ? sum / c : 0.0;
        if (sums[i] > peak) peak = sums[i];
        if (-sums[i] > peak) peak = -sums[i];
    }
    for (i = 0; i < n; ++i) {
        const double fade = n - i < JAKE_OUCH_CUT_FADE ? (double)(n - i) / JAKE_OUCH_CUT_FADE : 1.0;
        out[i] = jake_clamp16((int)(sums[i] * 32000.0 / peak * fade));
    }
    jake_encode_slot(y, out, blocks);
}

static void jake_build_voice(const uint8_t* disc, const int16_t* ouch) {
    static int16_t pcm[JAKE_VOICE_SIZE / 16 * 28], resampled[JAKE_VOICE_SIZE / 16 * 28];
    static int16_t cut[JAKE_VOICE_SIZE / 16 * 28];
    /* Skip the source's silent lead-in block; each slot gets its own. */
    const uint32_t n = jake_adpcm_decode(disc + 16, JAKE_VOICE_SIZE - 16u, pcm, sizeof(pcm) / sizeof(pcm[0]));
    const uint32_t m = (uint32_t)((uint64_t)n * SPIKE_YELL_PITCH / JAKE_VOICE_PITCH);
    const uint32_t full_blocks = (m + 27u) / 28u;
    uint32_t i;
    int y;
    for (i = 0; i < full_blocks * 28u; ++i) {
        const float at = (float)i * JAKE_VOICE_PITCH / SPIKE_YELL_PITCH;
        const uint32_t k = (uint32_t)at;
        const float t = at - (float)k;
        resampled[i] = i >= m || k + 1u >= n ? 0
            : (int16_t)((float)pcm[k] * (1.0f - t) + (float)pcm[k + 1u] * t);
    }
    for (y = 0; y < JAKE_YELL_COUNT; ++y) {
        const uint32_t room = g_yells[y].size / 16u - 1u;   /* after the lead-in */
        if (g_yells[y].source == JAKE_OUCH && ouch) {
            jake_build_ouch(y, ouch);
            continue;
        }
        if (g_yells[y].source != JAKE_YELL) {
            /* lead-in, then one silent block flagged start + end */
            memset(g_yells[y].jake, 0, 32);
            g_yells[y].jake[17] = 0x05u;
            g_yells[y].jake_size = 32u;
            continue;
        }
        const uint32_t blocks = full_blocks < room ? full_blocks : room;
        memcpy(cut, resampled, blocks * 28u * sizeof(cut[0]));
        if (blocks < full_blocks) {
            const uint32_t end = blocks * 28u;
            for (i = 0; i < JAKE_VOICE_FADE && i < end; ++i)
                cut[end - 1u - i] = (int16_t)((int32_t)cut[end - 1u - i] * (int32_t)i /
                                              JAKE_VOICE_FADE);
        }
        jake_encode_slot(y, cut, blocks);
    }
}

/*
 * XA ADPCM (4-bit stereo) for the "ouch": 18 sound groups of 128 bytes per
 * sector, 8 units each (even units left, odd right), filter state carried
 * across sectors. Only sectors of file 1 channel 1 are decoded.
 */
static const int XA_FILTERS[4][2] = { { 0, 0 }, { 60, 0 }, { 115, -52 }, { 98, -55 } };

enum { JAKE_OUCH_SPAN = (JAKE_OUCH_WARMUP + JAKE_OUCH_SECTORS) * JAKE_OUCH_STRIDE };

static int jake_load_ouch(int16_t* ouch) {
    static uint8_t sectors[JAKE_OUCH_SPAN][2352];
    const uint32_t first = JAKE_OUCH_LBA - JAKE_OUCH_WARMUP * JAKE_OUCH_STRIDE;
    int st[2][2] = { { 0, 0 }, { 0, 0 } };
    uint32_t got = 0, lba, size;
    int s;
    /* The sectors sit inside the cutscene stream on this disc's layout. */
    if (!psx_mod_disc_file_extent(JAKE_OUCH_FILE, &lba, &size) || first < lba ||
        JAKE_OUCH_LBA + JAKE_OUCH_SECTORS * JAKE_OUCH_STRIDE > lba + (size + 2047u) / 2048u ||
        !psx_mod_read_disc_raw_sectors(first, JAKE_OUCH_SPAN, sectors))
        return 0;
    for (s = -JAKE_OUCH_WARMUP; s < JAKE_OUCH_SECTORS; ++s) {
        const uint8_t* raw = sectors[(s + JAKE_OUCH_WARMUP) * JAKE_OUCH_STRIDE];
        int g, u, j;
        if (raw[16] != 1u || raw[17] != 1u || !(raw[18] & 0x04u) || raw[19] != 0x01u)
            return 0;
        for (g = 0; g < 18; ++g) {
            const uint8_t* grp = raw + 24 + g * 128;
            static int16_t lr[2][28 * 4];
            for (u = 0; u < 8; ++u) {
                const int ch = u & 1, hdr = grp[4 + u], shift = hdr & 15, flt = (hdr >> 4) & 3;
                for (j = 0; j < 28; ++j) {
                    int nib = (grp[16 + j * 4 + u / 2] >> ((u & 1) * 4)) & 15, v;
                    if (nib >= 8)
                        nib -= 16;
                    v = ((nib << 12) >> shift) +
                        ((st[ch][0] * XA_FILTERS[flt][0] + st[ch][1] * XA_FILTERS[flt][1] + 32) >> 6);
                    v = jake_clamp16(v);
                    st[ch][1] = st[ch][0];
                    st[ch][0] = v;
                    lr[ch][(u / 2) * 28 + j] = (int16_t)v;
                }
            }
            if (s < 0)
                continue;
            for (j = 0; j < 28 * 4 && got < JAKE_OUCH_SAMPLES; ++j)
                ouch[got++] = (int16_t)(((int)lr[0][j] + lr[1][j]) / 2);
        }
    }
    for (s = 0; s < JAKE_OUCH_FADE_IN; ++s)
        ouch[s] = (int16_t)(ouch[s] * s / JAKE_OUCH_FADE_IN);
    for (s = 0; s < JAKE_OUCH_FADE_OUT; ++s)
        ouch[JAKE_OUCH_SAMPLES - 1 - s] = (int16_t)(ouch[JAKE_OUCH_SAMPLES - 1 - s] * s / JAKE_OUCH_FADE_OUT);
    return got == JAKE_OUCH_SAMPLES;
}

/*
 * The game runs with the SPU IRQ enabled (SPUCNT bit 6) and its address at
 * 0, and setting the transfer address or writing through the FIFO is an IRQ
 * compare: putting the transfer address back to 0 afterwards raised an SPU
 * IRQ the game didn't expect, which wedged it in its interrupt handler
 * (seen on savestate loads, which bring Spike's samples back). So the
 * transfer address is left where we finish (the game sets its own before
 * every transfer), and a slot the IRQ address points into is skipped.
 */
static const uint32_t SPU_CONTROL = 0x1F801DAAu;
static const uint32_t SPU_IRQ_ADDRESS = 0x1F801DA4u;

static void jake_install_voice(void) {
    int y;
    for (y = 0; y < JAKE_YELL_COUNT; ++y) {
        uint8_t head[sizeof(g_yells[y].head)];
        const uint32_t irq = (uint32_t)psx_mod_read_half(SPU_IRQ_ADDRESS) << 3;
        uint32_t i;
        if (g_yells[y].jake_size == 0u ||
            spu_ram_peek(g_yells[y].spu, head, sizeof(head)) != sizeof(head) ||
            memcmp(head, g_yells[y].head, sizeof(head)) != 0)
            continue;
        if ((psx_mod_read_half(SPU_CONTROL) & 0x0040u) &&
            irq >= g_yells[y].spu && irq < g_yells[y].spu + g_yells[y].jake_size)
            continue;
        psx_mod_write_half(SPU_TRANSFER_ADDRESS, (uint16_t)(g_yells[y].spu >> 3));
        for (i = 0; i < g_yells[y].jake_size; i += 2u)
            psx_mod_write_half(SPU_TRANSFER_FIFO,
                               (uint16_t)(g_yells[y].jake[i] | (g_yells[y].jake[i + 1u] << 8)));
    }
}

/*
 * The level-clear outro streams its music from the CD as XA audio (file 1,
 * channel 6 - channel 7 is a copy - interleaved from LBA 40187, the channel
 * from 40193, every 8th sector), and Spike's "Yes!" is baked into it: the
 * first ~0.62 s, mono, before the stereo music comes in. There is no
 * voiceless copy, so the CD input volume is held at zero from the moment
 * that interleave starts being read until the voice has played out: 15
 * channel sectors (53 ms each; the voice is 12, the rest covers the SPU's
 * CD buffer), then faded back to what the game had set.
 */
static const uint32_t SPU_CD_VOLUME_LEFT = 0x1F801DB0u;
static const uint32_t SPU_CD_VOLUME_RIGHT = 0x1F801DB2u;
enum {
    OUTRO_XA_FILE = 1,
    OUTRO_XA_FIRST_LBA = 40187,
    OUTRO_XA_VOICE_LBA = 40193,         /* channel 6; 40194 for its copy */
    OUTRO_XA_INTERLEAVE = 8,
    OUTRO_VOICE_SECTORS = 15,
    OUTRO_FADE_FRAMES = 6,
    OUTRO_MAX_MUTE_FRAMES = 120         /* a savestate load mid-outro */
};

static struct {
    uint64_t seen;                      /* sector history entries handled */
    uint8_t muting, started;
    uint32_t sectors, fade, frames;
    uint16_t volume[2];                 /* the game's CD volume */
} g_outro;

static void jake_set_cd_volume(uint16_t left, uint16_t right) {
    psx_mod_write_half(SPU_CD_VOLUME_LEFT, left);
    psx_mod_write_half(SPU_CD_VOLUME_RIGHT, right);
}

static void jake_outro_vblank(void) {
    const CDROMSectorHistoryEntry* entries = NULL;
    const uint64_t total = cdrom_debug_get_sector_history(&entries);
    uint64_t seq = g_outro.seen;
    if (!g_outro.started) {             /* only what is read from now on */
        g_outro.started = 1u;
        g_outro.seen = total;
        return;
    }
    if (total - seq > CDROM_SECTOR_HISTORY_CAP)
        seq = total - CDROM_SECTOR_HISTORY_CAP;
    for (; entries && seq < total; ++seq) {
        const CDROMSectorHistoryEntry* e = &entries[seq % CDROM_SECTOR_HISTORY_CAP];
        if (e->seq != seq || e->xa_file != OUTRO_XA_FILE)
            continue;
        if (e->lba >= OUTRO_XA_FIRST_LBA && e->lba < OUTRO_XA_VOICE_LBA && !g_outro.muting) {
            g_outro.muting = 1u;
            g_outro.sectors = 0u;
            g_outro.fade = 0u;
            g_outro.frames = 0u;
            g_outro.volume[0] = psx_mod_read_half(SPU_CD_VOLUME_LEFT);
            g_outro.volume[1] = psx_mod_read_half(SPU_CD_VOLUME_RIGHT);
        }
        if (g_outro.muting && e->xa_audio_delivered && e->lba >= OUTRO_XA_VOICE_LBA &&
            (e->lba - OUTRO_XA_VOICE_LBA) % OUTRO_XA_INTERLEAVE <= 1 &&
            ++g_outro.sectors >= OUTRO_VOICE_SECTORS) {
            g_outro.muting = 0u;
            g_outro.fade = OUTRO_FADE_FRAMES;
        }
    }
    g_outro.seen = total;
    if (g_outro.muting && ++g_outro.frames > OUTRO_MAX_MUTE_FRAMES) {
        g_outro.muting = 0u;
        g_outro.fade = OUTRO_FADE_FRAMES;
    }
    if (g_outro.muting) {
        /* Keep whatever the game sets meanwhile for afterwards. */
        const uint16_t l = psx_mod_read_half(SPU_CD_VOLUME_LEFT);
        const uint16_t r = psx_mod_read_half(SPU_CD_VOLUME_RIGHT);
        if (l != 0u || r != 0u) {
            g_outro.volume[0] = l;
            g_outro.volume[1] = r;
        }
        jake_set_cd_volume(0u, 0u);
    } else if (g_outro.fade > 0u) {
        const int32_t step = OUTRO_FADE_FRAMES - (int32_t)--g_outro.fade;
        jake_set_cd_volume((uint16_t)((int16_t)g_outro.volume[0] * step / OUTRO_FADE_FRAMES),
                           (uint16_t)((int16_t)g_outro.volume[1] * step / OUTRO_FADE_FRAMES));
    }
}

/*
 * Spike's line as he warps into a level ("Let's go!", "I'm going for it!",
 * ...) is a CD-XA clip: file 1 of the voice interleave at LBA 51083 (16
 * channels, channel c's sectors at 51083 + c + 16n). Channels 0-3 are four
 * warp-in lines, 1.17-1.71 s mono at 18.9 kHz, 4-7 the same four again and
 * 8-11 four more Spike lines (1.39-2.45 s; 12-15 are empty); the last sector
 * of each is flagged end-of-file. All are muted. Nothing else streams from the CD then (level music is
 * sequenced), so the CD input is muted from the clip's first delivered
 * sector until a while after its last one.
 */
enum {
    SPAWN_XA_FILE = 1,
    SPAWN_XA_CHANNELS = 12,             /* 0..11 */
    SPAWN_XA_FIRST_LBA = 51083,
    SPAWN_XA_LAST_LBA = 51460,          /* channel 11 runs to 51446 */
    XA_SUBMODE_EOF = 0x80,
    SPAWN_TAIL_FRAMES = 30,             /* the SPU's CD buffer plays out */
    SPAWN_MAX_MUTE_FRAMES = 240
};

static struct {
    uint64_t seen;
    uint8_t muting, started;
    uint32_t tail, fade, frames;
    uint16_t volume[2];
} g_spawn;

static void jake_spawn_vblank(void) {
    const CDROMSectorHistoryEntry* entries = NULL;
    const uint64_t total = cdrom_debug_get_sector_history(&entries);
    uint64_t seq = g_spawn.seen;
    if (!g_spawn.started) {
        g_spawn.started = 1u;
        g_spawn.seen = total;
        return;
    }
    if (total - seq > CDROM_SECTOR_HISTORY_CAP)
        seq = total - CDROM_SECTOR_HISTORY_CAP;
    for (; entries && seq < total; ++seq) {
        const CDROMSectorHistoryEntry* e = &entries[seq % CDROM_SECTOR_HISTORY_CAP];
        if (e->seq != seq || e->xa_file != SPAWN_XA_FILE || e->xa_channel >= SPAWN_XA_CHANNELS ||
            !e->xa_audio_delivered || e->lba < SPAWN_XA_FIRST_LBA || e->lba > SPAWN_XA_LAST_LBA)
            continue;
        if (!g_spawn.muting) {
            g_spawn.muting = 1u;
            g_spawn.fade = 0u;
            g_spawn.frames = 0u;
            g_spawn.volume[0] = psx_mod_read_half(SPU_CD_VOLUME_LEFT);
            g_spawn.volume[1] = psx_mod_read_half(SPU_CD_VOLUME_RIGHT);
        }
        g_spawn.tail = (e->xa_submode & XA_SUBMODE_EOF) ? SPAWN_TAIL_FRAMES : 0u;
    }
    g_spawn.seen = total;
    if (g_spawn.muting && ((g_spawn.tail > 0u && --g_spawn.tail == 0u) ||
                           ++g_spawn.frames > SPAWN_MAX_MUTE_FRAMES)) {
        g_spawn.muting = 0u;
        g_spawn.fade = OUTRO_FADE_FRAMES;
    }
    if (g_spawn.muting) {
        const uint16_t l = psx_mod_read_half(SPU_CD_VOLUME_LEFT);
        const uint16_t r = psx_mod_read_half(SPU_CD_VOLUME_RIGHT);
        if (l != 0u || r != 0u) {
            g_spawn.volume[0] = l;
            g_spawn.volume[1] = r;
        }
        jake_set_cd_volume(0u, 0u);
    } else if (g_spawn.fade > 0u) {
        const int32_t step = OUTRO_FADE_FRAMES - (int32_t)--g_spawn.fade;
        jake_set_cd_volume((uint16_t)((int16_t)g_spawn.volume[0] * step / OUTRO_FADE_FRAMES),
                           (uint16_t)((int16_t)g_spawn.volume[1] * step / OUTRO_FADE_FRAMES));
    }
}

static void jake_click_vblank(void);

/* ---- Purple gadgets ----------------------------------------------------- */

/*
 * Jake's Stun Club and Time Net are purple. Colour words are R, G, B bytes.
 *
 * Club: the light blade is model entry 0x800EC0F0, whose colour pointer
 * (entry +4) leads to blue, teal and a white core. While it still points at
 * those stock colours it is pointed at a purple set of ours. The Power Up
 * Gadgets mod swaps the same pointer to red while the club is powered and
 * puts back whatever was there, so powered stays red and unpowered purple.
 * The blade's halo is a glow beam, 0x8002020C called from the club draw
 * (returning to 0x8009AD34) with its colour in a3: stock light blue becomes
 * purple (a red one from the power-up is left alone).
 *
 * Net: drawn by the transform 0x80016A60 called from 0x80099324 with the net
 * object in a0; its colour list (object +4, count u16 at header +8, 25 greyish
 * blues) is copied with every colour turned to the purple hue, keeping its
 * lightness, and the object pointed at the copy (redone after a level load).
 */
static const uint32_t JAKE_CLUB_BLADE_ENTRY = 0x800EC0F0u;
static const uint32_t JAKE_CLUB_STOCK[3] = { 0x00F80000u, 0x00808000u, 0x00F8F8F8u };
/* Violet rather than magenta: a first set with more red read as pink. */
static const uint32_t JAKE_CLUB_PURPLE[3] = {
    0x00F80058u,   /* violet (was blue) */
    0x00880030u,   /* dark violet (was teal) */
    0x00F8A0B0u    /* pale violet core (was white) */
};
static const uint32_t JAKE_GLOW_BEAM_FUNCTION = 0x8002020Cu;
static const uint32_t JAKE_CLUB_HALO_RETURN = 0x8009AD34u;
static const uint32_t JAKE_CLUB_HALO_STOCK = 0x00FF8080u;
static const uint32_t JAKE_CLUB_HALO_PURPLE = 0x00BE005Au;   /* deep purple glow */
static const uint32_t JAKE_NET_TRANSFORM_RETURN = 0x8009932Cu;
static const float JAKE_PURPLE_HUE = 261.0f / 360.0f;   /* the club blade's violet */
enum { JAKE_NET_MAX_COLOURS = 64 };

static uint32_t g_club_purple, g_net_purple, g_net_source;

static int jake_ram_pointer(uint32_t p) {
    return p >= 0x80000000u && p < 0x80200000u;
}

/* Point a gadget model entry's colour list at ours while it still points at
 * the stock one (so another mod's colours, e.g. the powered red club, are
 * left alone). */
static void jake_swap_colours(uint32_t model_entry, const uint32_t* stock, const uint32_t* ours,
                              int count, uint32_t* list) {
    const uint32_t entry = model_entry + 4u, colours = psx_mod_read_word(entry);
    int i;
    if (!jake_ram_pointer(colours) || colours == *list)
        return;
    for (i = 0; i < count; ++i)
        if (psx_mod_read_word(colours + (uint32_t)i * 4u) != stock[i])
            return;
    if (*list == 0u) {
        *list = psx_mod_alloc_guest_memory((uint32_t)count * 4u, 16u);
        if (*list == 0u)
            return;
        for (i = 0; i < count; ++i)
            psx_mod_write_word(*list + (uint32_t)i * 4u, ours[i]);
    }
    psx_mod_write_word(entry, *list);
}

static void jake_purple_club(void) {
    jake_swap_colours(JAKE_CLUB_BLADE_ENTRY, JAKE_CLUB_STOCK, JAKE_CLUB_PURPLE, 3, &g_club_purple);
}

/*
 * Super Hoop: model entry 0x800EC0A0, textured with a 4-bit stripe texture
 * whose palette (VRAM 944,503; only the hoop uses it) is white, yellow and a
 * dark edge, tinted by the model's cyan: white -> light blue, yellow ->
 * green. Cyan has no red, so no tint can give purple: the tint goes neutral
 * and the palette itself becomes blue (as the white stripes looked), violet
 * (the club's) and the dark edge as it looked.
 *
 * The gadget ring's icons (sprites, one palette each at x 944) match: the
 * club's and net's blues and the hoop's greens and yellow-green turn to the
 * same violet, lightness kept (the hoop icon ends up blue and violet like
 * the hoop). Values from re/ icon palette dumps, recoloured at hue 265.
 *
 * Each palette below is uploaded with the face (jake_upload_face) whenever
 * VRAM still holds exactly the stock one, so a level that keeps something
 * else there is never written over.
 */
static const uint32_t JAKE_HOOP_ENTRY = 0x800EC0A0u;
static const uint32_t JAKE_HOOP_STOCK[3] = { 0x00F8F800u, 0x00686868u, 0x00707070u };
static const uint32_t JAKE_HOOP_NEUTRAL[3] = { 0x00808080u, 0x00686868u, 0x00707070u };
static uint32_t g_hoop_neutral;
uint16_t gpu_vram_peek(int x, int y);

typedef struct JakeClutSwap {
    uint16_t x, y, count;          /* count even: copies are whole words */
    uint16_t stock[16], ours[16];
} JakeClutSwap;
static const JakeClutSwap JAKE_CLUT_SWAPS[] = {
    /* hoop stripes: blue (0,31,31) was white, violet (11,0,31) was yellow,
     * the dark edge (0,19,31) as it looked tinted, 4th unused */
    { 944u, 503u, 4u, { 0xFFFFu, 0x83FFu, 0xC950u, 0x0000u },
                      { 0xFFE0u, 0xFC0Bu, 0xFE60u, 0x0000u } },
    /* club icon */
    { 944u, 456u, 16u,
      { 0x0000u, 0xA929u, 0xFECFu, 0xFDA9u, 0xFC48u, 0xD400u, 0xDCEBu, 0xDE73u,
        0xF79Cu, 0x8400u, 0xBDEFu, 0xFE12u, 0xFF94u, 0x8000u, 0xFFF9u, 0xFFFFu },
      { 0x0000u, 0xA929u, 0xFDF6u, 0xFD32u, 0xFC4Eu, 0xD409u, 0xDCEEu, 0xDE73u,
        0xF79Cu, 0x8400u, 0xBDEFu, 0xFE16u, 0xFE99u, 0x8000u, 0xFF3Bu, 0xFFFFu } },
    /* hoop icon */
    { 944u, 451u, 16u,
      { 0x0000u, 0xFE60u, 0xA3E0u, 0xE109u, 0xA286u, 0x8400u, 0xA160u, 0x83F7u,
        0x8000u, 0xFEF0u, 0xFF72u, 0x8000u, 0x8000u, 0x8000u, 0xFFFFu, 0x8000u },
      { 0x0000u, 0xFE60u, 0xFC0Du, 0xE109u, 0xD0CCu, 0x8400u, 0xAC05u, 0xFC0Du,
        0x8000u, 0xFEF0u, 0xFF72u, 0x8000u, 0x8000u, 0x8000u, 0xFFFFu, 0x8000u } },
    /* net icon */
    { 944u, 448u, 16u,
      { 0x0000u, 0xB98Eu, 0xFFFFu, 0xFF72u, 0xFF2Bu, 0xFFFBu, 0xFAA1u, 0xD675u,
        0xA108u, 0x8400u, 0xC5F1u, 0xEAF9u, 0x8000u, 0xE22Au, 0xE28Cu, 0x8000u },
      { 0x0000u, 0xB98Eu, 0xFFFFu, 0xFE57u, 0xFD73u, 0xFF7Du, 0xF82Du, 0xD675u,
        0xA108u, 0x8400u, 0xC5F1u, 0xEAF9u, 0x8000u, 0xE150u, 0xE191u, 0x8000u } },
    /* net mesh texture (the see-through bag, cyan to blue) */
    { 944u, 508u, 16u,
      { 0xFFFFu, 0xFFFAu, 0xFFF0u, 0xFFEAu, 0xFFB0u, 0xFEA5u, 0xFF10u, 0xFE07u,
        0xFE4Bu, 0xF960u, 0xFDA4u, 0xE0C3u, 0xD440u, 0x9000u, 0xB400u, 0x8000u },
      { 0xFFFFu, 0xFF5Cu, 0xFE16u, 0xFD53u, 0xFE16u, 0xFCB0u, 0xFE16u, 0xFCF1u,
        0xFD73u, 0xF80Cu, 0xFC8Fu, 0xE06Cu, 0xD409u, 0x9002u, 0xB405u, 0x8000u } },
    /*
     * The rest of the gadgets, same rule (colours of saturation >= 0.25 to
     * hue 265, greys and metal left alone): ring icons by gadget id (2 radar,
     * 3 slingshot, 5 magic punch, 6 sky flyer, 7 RC car) and the gadget
     * textures (only gadget models draw with these palettes). The punch's
     * spring (944,502) and the car's gold (944,505) keep their colours, and
     * the punch and car icons and the car body change only their reds.
     */
    /* radar icon */
    { 944u, 459u, 16u,
      { 0x0000u, 0xA108u, 0xFFFFu, 0xEF5Au, 0xD6B5u, 0xE2F7u, 0xAD6Bu, 0x805Du,
        0x8011u, 0xCA52u, 0x8400u, 0xB1BFu, 0xBDEFu, 0xC210u, 0x0000u, 0x0000u },
      { 0x0000u, 0xA108u, 0xFFFFu, 0xEF5Au, 0xD6B5u, 0xE2F7u, 0xAD6Bu, 0xF40Cu,
        0xC407u, 0xCA52u, 0x8400u, 0xFD94u, 0xBDEFu, 0xC210u, 0x0000u, 0x0000u } },
    /* slingshot icon */
    { 944u, 455u, 16u,
      { 0x0000u, 0x810Bu, 0x8214u, 0x839Fu, 0x8400u, 0xFFFFu, 0xA2F7u, 0xE2D6u,
        0xC1EEu, 0xD7FFu, 0x8000u, 0x8000u, 0x8000u, 0x8000u, 0x8000u, 0x8000u },
      { 0x0000u, 0xAC05u, 0xD008u, 0xFC0Du, 0x8400u, 0xFFFFu, 0xDD0Eu, 0xE2D6u,
        0xC1EEu, 0xFEB9u, 0x8000u, 0x8000u, 0x8000u, 0x8000u, 0x8000u, 0x8000u } },
    /* magic punch icon: only the red glove (the yellow handle stays) */
    { 944u, 454u, 16u,
      { 0x0000u, 0x800Du, 0x801Fu, 0x819Bu, 0x9018u, 0x82BFu, 0x800Du, 0x8014u,
        0x99BFu, 0xFFFFu, 0x8400u, 0x9115u, 0xA3C0u, 0x8000u, 0x8000u, 0x8000u },
      { 0x0000u, 0xB405u, 0xFC0Du, 0x819Bu, 0xE00Au, 0x82BFu, 0xB405u, 0xD008u,
        0xFCD0u, 0xFFFFu, 0x8400u, 0xD48Bu, 0xA3C0u, 0x8000u, 0x8000u, 0x8000u } },
    /* sky flyer icon */
    { 944u, 458u, 16u,
      { 0x0000u, 0xCE73u, 0x8400u, 0xAB80u, 0x96A0u, 0x8300u, 0x9F20u, 0x9D20u,
        0xFFFFu, 0xB18Cu, 0x8000u, 0xA3D2u, 0xBBD9u, 0xBBAFu, 0xDED6u, 0x8000u },
      { 0x0000u, 0xCE73u, 0x8400u, 0xF00Cu, 0xD409u, 0xE00Au, 0xE40Au, 0xA404u,
        0xFFFFu, 0xB18Cu, 0x8000u, 0xF911u, 0xF9D5u, 0xF5D4u, 0xDED6u, 0x8000u } },
    /* RC car icon: only the red */
    { 944u, 457u, 16u,
      { 0x0000u, 0x8C63u, 0x98C6u, 0xB9CEu, 0x8400u, 0x801Fu, 0xFFFFu, 0x810Du,
        0x827Du, 0x83FFu, 0xCA2Cu, 0xF756u, 0x8C10u, 0xAD6Bu, 0x9CE7u, 0xDEF7u },
      { 0x0000u, 0x8C63u, 0x98C6u, 0xB9CEu, 0x8400u, 0xFC0Du, 0xFFFFu, 0x810Du,
        0x827Du, 0x83FFu, 0xCA2Cu, 0xF756u, 0xC007u, 0xAD6Bu, 0x9CE7u, 0xDEF7u } },
    /* RC car body texture: only the reds (its yellow and gold stay) */
    { 944u, 509u, 16u,
      { 0xFFFFu, 0x8C63u, 0xD6B7u, 0xAD6Eu, 0xC639u, 0xB9D7u, 0xB9DFu, 0x800Du,
        0x8015u, 0x887Au, 0x88B5u, 0x9D2Du, 0x94C7u, 0x8295u, 0x81C6u, 0x0000u },
      { 0xFFFFu, 0x8C63u, 0xD6B7u, 0xAD6Eu, 0xE634u, 0xDDD2u, 0xFDD5u, 0xB405u,
        0xD409u, 0xE84Cu, 0xD44Au, 0xB4E9u, 0x94C7u, 0x8295u, 0x81C6u, 0x0000u } },
    /* radar texture: greys stay, the red light goes violet */
    { 944u, 491u, 16u,
      { 0xFFFFu, 0x9084u, 0x98C6u, 0xA108u, 0xA529u, 0xAD6Bu, 0xB18Cu, 0xCE73u,
        0x801Fu, 0x880Bu, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u },
      { 0xFFFFu, 0x9084u, 0x98C6u, 0xA108u, 0xA529u, 0xAD6Bu, 0xB18Cu, 0xCE73u,
        0xFC0Du, 0xAC05u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u } },
    /* sky flyer flight glow (the green disc while flying) */
    { 944u, 470u, 16u,
      { 0x8020u, 0xCF8Fu, 0xBB69u, 0x9E23u, 0xE3B6u, 0xAAE4u, 0x8D01u, 0xC36Cu,
        0xDBB3u, 0xEFFAu, 0xB346u, 0x88A1u, 0x9582u, 0xD391u, 0xEBD8u, 0xC78Eu },
      { 0x8020u, 0xF1F4u, 0xED30u, 0xC469u, 0xF6D9u, 0xDC8Cu, 0xA024u, 0xED92u,
        0xF677u, 0xFF5Cu, 0xE8CEu, 0x9423u, 0xB046u, 0xF236u, 0xFB1Au, 0xF1D4u } },
    /* sky flyer texture: greens */
    { 944u, 504u, 16u,
      { 0xFFFFu, 0xB7DFu, 0xF3FFu, 0xD7FFu, 0x9BDCu, 0x8BD9u, 0x87D6u, 0x83CFu,
        0x83E9u, 0x83E4u, 0x8781u, 0x8040u, 0x8E60u, 0x9180u, 0x8CC0u, 0x8000u },
      { 0xFFFFu, 0xFDB4u, 0xFF9Du, 0xFEB9u, 0xF8D0u, 0xF84Eu, 0xF82Du, 0xF80Cu,
        0xFC0Du, 0xFC0Du, 0xF02Cu, 0x8040u, 0xCC08u, 0xB005u, 0x9802u, 0x8000u } },
};
enum { JAKE_CLUT_SWAP_COUNT = sizeof(JAKE_CLUT_SWAPS) / sizeof(JAKE_CLUT_SWAPS[0]) };

static void jake_purple_hoop(void) {
    jake_swap_colours(JAKE_HOOP_ENTRY, JAKE_HOOP_STOCK, JAKE_HOOP_NEUTRAL, 3, &g_hoop_neutral);
}

static int jake_clut_is_stock(const JakeClutSwap* swap) {
    int i;
    for (i = 0; i < swap->count; ++i)
        if (gpu_vram_peek(swap->x + i, swap->y) != swap->stock[i])
            return 0;
    return 1;
}

static float jake_hue_channel(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 0.5f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

/* Same lightness, purple hue. JAKE_TINT_ALL: greys get enough saturation to
 * read purple; JAKE_TINT_COLOURS: colours below 0.15 saturation stay as they
 * are; JAKE_TINT_REDS: only reds (hue within 30 degrees of red) change. */
enum { JAKE_TINT_ALL, JAKE_TINT_COLOURS, JAKE_TINT_REDS };

static uint32_t jake_purple_colour(uint32_t c, int mode) {
    const float r = (float)(c & 0xFFu) / 255.0f, g = (float)((c >> 8) & 0xFFu) / 255.0f;
    const float b = (float)((c >> 16) & 0xFFu) / 255.0f;
    const float hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const float lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
    const float l = (hi + lo) * 0.5f;
    float s = hi == lo ? 0.0f : (l > 0.5f ? (hi - lo) / (2.0f - hi - lo) : (hi - lo) / (hi + lo));
    float q, p;
    if (mode != JAKE_TINT_ALL && (s < 0.15f || hi < 0.03f))
        return c;
    if (mode == JAKE_TINT_REDS) {
        /* red is the top channel and blue the bottom or near it */
        const float d = hi - lo;
        const float hue = r == hi ? 60.0f * (g - b) / d : g == hi ? 60.0f * (2.0f + (b - r) / d)
                                                                  : 60.0f * (4.0f + (r - g) / d);
        if (hue > 30.0f && hue < 330.0f)
            return c;
    }
    s = s * 1.5f < 0.35f ? 0.35f : s * 1.5f > 1.0f ? 1.0f : s * 1.5f;
    q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
    p = 2.0f * l - q;
    return (uint32_t)(jake_hue_channel(p, q, JAKE_PURPLE_HUE + 1.0f / 3.0f) * 255.0f + 0.5f) |
           (uint32_t)(jake_hue_channel(p, q, JAKE_PURPLE_HUE) * 255.0f + 0.5f) << 8 |
           (uint32_t)(jake_hue_channel(p, q, JAKE_PURPLE_HUE - 1.0f / 3.0f) * 255.0f + 0.5f) << 16 |
           (c & 0xFF000000u);
}

static void jake_purple_net(uint32_t object) {
    const uint32_t header = psx_mod_read_word(object), colours = psx_mod_read_word(object + 4u);
    uint32_t count, i;
    if (!jake_ram_pointer(header) || !jake_ram_pointer(colours) || colours == g_net_purple)
        return;
    count = psx_mod_read_half(header + 8u);
    if (count == 0u || count > JAKE_NET_MAX_COLOURS)
        return;
    if (g_net_purple == 0u && (g_net_purple = psx_mod_alloc_guest_memory(JAKE_NET_MAX_COLOURS * 4u, 16u)) == 0u)
        return;
    if (colours != g_net_source) {
        for (i = 0; i < count; ++i)
            psx_mod_write_word(g_net_purple + i * 4u,
                               jake_purple_colour(psx_mod_read_word(colours + i * 4u), JAKE_TINT_ALL));
        g_net_source = colours;
    }
    psx_mod_write_word(object + 4u, g_net_purple);
}

/* The gadget menu (Select) draws the net from its model-table entry, which
 * has the same layout (header, colours, ...): recoloured the same way while
 * it still holds the stock net colours (25, starting (0,8,8), (16,24,32)). */
static const uint32_t JAKE_NET_TABLE_ENTRY = 0x800EC070u;

static void jake_purple_net_entry(void) {
    const uint32_t header = psx_mod_read_word(JAKE_NET_TABLE_ENTRY);
    const uint32_t colours = psx_mod_read_word(JAKE_NET_TABLE_ENTRY + 4u);
    if (!jake_ram_pointer(header) || !jake_ram_pointer(colours) ||
        psx_mod_read_half(header + 8u) != 25u ||
        psx_mod_read_word(colours) != 0x00080800u || psx_mod_read_word(colours + 4u) != 0x00201810u)
        return;
    jake_purple_net(JAKE_NET_TABLE_ENTRY);
}

/*
 * Every other gadget model in the table (in hand and in the gadget menu):
 * each entry, recognised by its colour count and first two stock colours,
 * gets a copy of its colour list with the coloured parts violet (greys and
 * metal kept) and is pointed at it. Entries holding anything else - another
 * level's model, or the Power Up Gadgets mod's SMG in the slingshot slot -
 * are left alone.
 */
typedef struct JakeModelTint {
    uint32_t entry;
    uint32_t count, first, second;
    int mode;
} JakeModelTint;
/* Found by flashing each entry in the gadget menu; the entries that don't
 * show there change only their reds. */
static const JakeModelTint JAKE_MODEL_TINTS[] = {
    { 0x800EC060u, 32u, 0x00181010u, 0x00201810u, JAKE_TINT_COLOURS },  /* club handle */
    { 0x800EC080u, 19u, 0x00000018u, 0x00000020u, JAKE_TINT_COLOURS },  /* radar stand */
    { 0x800EC090u,  8u, 0x00004040u, 0x00004848u, JAKE_TINT_COLOURS },  /* slingshot */
    { 0x800EC0B0u,  2u, 0x00004080u, 0x0000F8F8u, JAKE_TINT_REDS },
    { 0x800EC0E0u,  4u, 0x00808080u, 0x000000B0u, JAKE_TINT_REDS },
    { 0x800EC100u, 13u, 0x00000000u, 0x00000040u, JAKE_TINT_REDS },     /* punch glove */
    { 0x800EC130u,  7u, 0x00000000u, 0x00000030u, JAKE_TINT_REDS },
    { 0x800EC140u,  6u, 0x00000038u, 0x00808080u, JAKE_TINT_REDS },
    { 0x800EC150u, 26u, 0x00000000u, 0x00000018u, JAKE_TINT_COLOURS },  /* radar dish */
    { 0x800EC170u,  4u, 0x00000000u, 0x0000F800u, JAKE_TINT_REDS },
};
enum { JAKE_MODEL_TINT_COUNT = sizeof(JAKE_MODEL_TINTS) / sizeof(JAKE_MODEL_TINTS[0]) };
static uint32_t g_tint_lists[JAKE_MODEL_TINT_COUNT];

static void jake_tint_gadgets(void) {
    int t;
    for (t = 0; t < JAKE_MODEL_TINT_COUNT; ++t) {
        const JakeModelTint* tint = &JAKE_MODEL_TINTS[t];
        const uint32_t header = psx_mod_read_word(tint->entry);
        const uint32_t colours = psx_mod_read_word(tint->entry + 4u);
        uint32_t i;
        if (!jake_ram_pointer(header) || !jake_ram_pointer(colours) || colours == g_tint_lists[t] ||
            psx_mod_read_half(header + 8u) != tint->count ||
            psx_mod_read_word(colours) != tint->first || psx_mod_read_word(colours + 4u) != tint->second)
            continue;
        if (g_tint_lists[t] == 0u &&
            (g_tint_lists[t] = psx_mod_alloc_guest_memory(tint->count * 4u, 16u)) == 0u)
            continue;
        for (i = 0; i < tint->count; ++i)
            psx_mod_write_word(g_tint_lists[t] + i * 4u,
                               jake_purple_colour(psx_mod_read_word(colours + i * 4u), tint->mode));
        psx_mod_write_word(tint->entry + 4u, g_tint_lists[t]);
    }
}

/*
 * Club spin trail: 0x8001F954 writes up to 12 translucent gouraud quads from
 * the packet pointer (0x800EC354) on, each 0x28 bytes: tag (length 9), draw
 * mode 0xE1000220, then four colour + vertex pairs (code 0x3A). The colour is
 * built in code as fade << 16, i.e. blue only, so there is nothing to change
 * in memory: the start of each call's packets is noted here and, before the
 * frame goes to the GPU (the DrawOTag hook), every quad that still matches
 * that shape gets violet in place of blue, same fade.
 */
static const uint32_t JAKE_TRAIL_FUNCTION = 0x8001F954u;
static const uint32_t JAKE_PACKET_POINTER = 0x800EC354u;
static const uint32_t JAKE_TRAIL_DRAW_MODE = 0xE1000220u;
enum { JAKE_TRAIL_QUAD = 0x28, JAKE_TRAIL_MAX_QUADS = 12, JAKE_TRAIL_MAX_CALLS = 8 };
static uint32_t g_trail_starts[JAKE_TRAIL_MAX_CALLS];
static uint32_t g_trail_count;

static void jake_trail_entry(struct CPUState* cpu, uint32_t address) {
    (void)cpu;
    (void)address;
    if (g_enabled && g_trail_count < JAKE_TRAIL_MAX_CALLS)
        g_trail_starts[g_trail_count++] = psx_mod_read_word(JAKE_PACKET_POINTER);
}

static void jake_purple_trail(void) {
    uint32_t c, q, k;
    for (c = 0; c < g_trail_count; ++c) {
        for (q = 0; q < JAKE_TRAIL_MAX_QUADS; ++q) {
            const uint32_t p = g_trail_starts[c] + q * JAKE_TRAIL_QUAD;
            if (!jake_ram_pointer(p) || psx_mod_read_byte(p + 3u) != 9u ||
                psx_mod_read_word(p + 4u) != JAKE_TRAIL_DRAW_MODE || psx_mod_read_byte(p + 0xBu) != 0x3Au)
                break;
            for (k = 0; k < 4; ++k) {
                const uint32_t at = p + 8u + k * 8u, w = psx_mod_read_word(at);
                const uint32_t fade = (w >> 16) & 0xFFu;
                if ((w & 0xFFFFu) != 0u)
                    break;   /* already recoloured, or not ours */
                /* violet: red at half the blue (less reads periwinkle over grass) */
                psx_mod_write_word(at, (w & 0xFF000000u) | fade << 16 | (fade / 2u));
            }
        }
    }
    g_trail_count = 0;
}

static void jake_glow_beam_entry(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (g_enabled && cpu->gpr[31] == JAKE_CLUB_HALO_RETURN && cpu->gpr[7] == JAKE_CLUB_HALO_STOCK)
        cpu->gpr[7] = JAKE_CLUB_HALO_PURPLE;
}

static void jake_load_from_disc(void);

static void jake_vblank(void) {
    static uint32_t ticks;
    static uint8_t loaded;
    if (!g_enabled)
        return;
    if (!loaded) {
        loaded = 1u;
        jake_load_from_disc();
    }
    jake_purple_club();
    jake_purple_hoop();
    jake_purple_net_entry();
    jake_tint_gadgets();
    if (ticks++ % JAKE_VOICE_CHECK_PERIOD == 0u)
        jake_install_voice();
    jake_outro_vblank();
    jake_spawn_vblank();
    jake_click_vblank();
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
static int jake_build_look(int look, uint32_t spike_object) {
    uint8_t mesh[0x1E38];
    const uint32_t size = JAKE_LOOK_DISC[look].mesh_size;
    uint32_t base, colours, parts, verts, polys, k, jake_verts, spike_verts, vert_list;
    memcpy(mesh, g_look_mesh[look], size);
    colours = 0x0Cu;
    parts = colours + jake_get16(mesh + 8) * 4u;
    verts = parts + jake_get16(mesh + 4) * JAKE_PART_ENTRY_SIZE;
    polys = verts + jake_get16(mesh + 6) * 8u;
    if (polys >= size || !jake_retarget_polys(mesh, polys, size, jake_get16(mesh + 2))) {
        fprintf(stderr, "play-as-jake: Jake's mesh did not parse\n");
        return 0;
    }
    jake_verts = jake_get16(mesh + 6);
    spike_verts = psx_mod_read_half(psx_mod_read_word(spike_object) + 6u);
    if (spike_verts < jake_verts)
        spike_verts = jake_verts;
    base = psx_mod_alloc_guest_memory(size + spike_verts * 8u, 16u);
    if (base == 0u)
        return 0;
    for (k = 0; k < size; k += 4)
        psx_mod_write_word(base + k, (uint32_t)mesh[k] | ((uint32_t)mesh[k + 1] << 8) |
                                     ((uint32_t)mesh[k + 2] << 16) | ((uint32_t)mesh[k + 3] << 24));
    vert_list = base + size;
    for (k = 0; k < jake_verts * 8u; k += 4)
        psx_mod_write_word(vert_list + k, psx_mod_read_word(base + verts + k));
    for (; k < spike_verts * 8u; k += 4)
        psx_mod_write_word(vert_list + k,
                           psx_mod_read_word(psx_mod_read_word(spike_object + 0x10u) + k));
    g_look_words[look][0] = base;
    g_look_words[look][1] = base + colours;
    g_look_words[look][2] = base + polys;
    g_look_words[look][3] = base + parts;
    g_look_words[look][4] = vert_list;
    return 1;
}

/* Make a look the current one: its mesh words and face (uploaded with the
 * next batch). */
static void jake_select_look(int look) {
    if (!g_look_built[look])
        return;
    g_look = look;
    memcpy(g_jake_mesh, g_look_words[look], sizeof(g_jake_mesh));
    memcpy(g_face_clut, g_look_clut[look], sizeof(g_face_clut));
    memcpy(g_face_texels, g_look_texels[look], sizeof(g_face_texels));
}

static void jake_prepare_mesh(uint32_t spike_object) {
    static uint8_t tried;
    int look;
    if (tried || !g_disc_ok)
        return;
    tried = 1u;
    for (look = 0; look < JAKE_LOOKS; ++look)
        g_look_built[look] = g_look_ok[look] && jake_build_look(look, spike_object);
    if (!g_look_built[JAKE_EVIL])
        return;
    jake_select_look(g_look_built[g_look] ? g_look : JAKE_EVIL);
    g_jake_mesh_ready = 1u;
}

/* One of our Jake meshes (either look). */
static int jake_is_our_mesh(uint32_t header) {
    return header != 0u && (header == g_look_words[JAKE_EVIL][0] ||
                            header == g_look_words[JAKE_NORMAL][0]);
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

/* The HUD's purple hair (blue above red above green) turns blue for the
 * NORMAL look by swapping red and green. */
static uint16_t jake_look_colour(uint16_t c) {
    const uint16_t r = c & 31u, g = (c >> 5) & 31u, b = (c >> 10) & 31u;
    if (g_look != JAKE_NORMAL || !(b > r && r > g + 1u))
        return c;
    return (uint16_t)((c & 0x8000u) | g | (r << 5) | (b << 10));
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
    for (row = 0; row < JAKE_CLUT_SWAP_COUNT; ++row) {
        const JakeClutSwap* swap = &JAKE_CLUT_SWAPS[row];
        if (!jake_clut_is_stock(swap))
            continue;
        words[0] = 0xA0000000u;
        words[1] = ((uint32_t)swap->y << 16) | swap->x;
        words[2] = (1u << 16) | swap->count;
        for (k = 0; k < swap->count; k += 2)
            words[3 + k / 2] = swap->ours[k] | ((uint32_t)swap->ours[k + 1] << 16);
        jake_link_packet(far_entry, words, 3u + swap->count / 2u);
    }
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
            words[3 + k / 2] = jake_look_colour(icon->palette[k]) |
                               ((uint32_t)jake_look_colour(icon->palette[k + 1]) << 16);
        jake_link_packet(far_entry, words, 3u + 8u);
    }
    words[0] = 0xA0000000u;
    words[1] = ((uint32_t)JAKE_LIFE_CLUT_Y << 16) | JAKE_LIFE_CLUT_X;
    words[2] = (1u << 16) | 16u;
    for (k = 0; k < 16; k += 2)
        words[3 + k / 2] = JAKE_LIFE_PALETTE[k] | ((uint32_t)JAKE_LIFE_PALETTE[k + 1] << 16);
    jake_link_packet(far_entry, words, 3u + 8u);
    for (row = 0; row < JAKE_HUD_ART_COUNT; ++row) {
        const uint32_t count = (uint32_t)JAKE_HUD_ART[row].w_halfwords * JAKE_HUD_ART[row].h;
        words[0] = 0xA0000000u;
        words[1] = ((uint32_t)JAKE_HUD_ART[row].y << 16) | JAKE_HUD_ART[row].x;
        words[2] = ((uint32_t)JAKE_HUD_ART[row].h << 16) | JAKE_HUD_ART[row].w_halfwords;
        for (k = 0; k < count; k += 2)
            words[3 + k / 2] = JAKE_HUD_ART[row].texels[k] |
                               ((uint32_t)JAKE_HUD_ART[row].texels[k + 1] << 16);
        jake_link_packet(far_entry, words, 3u + count / 2u);
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

static void jake_pose_tail_rest(uint32_t matrices) {
    JakeAffine head, tail;
    int k;
    jake_read_affine(jake_part_matrix(matrices, JAKE_HEAD_PART), &head);
    for (k = 0; k < 2; ++k) {
        jake_multiply(&head, &JAKE_TAIL_REST[k], &tail);
        jake_write_affine(jake_part_matrix(matrices, JAKE_TAIL_PART + k), &tail);
    }
}

/* Jake's tail as his own skeleton holds it this frame, on another head. */
static void jake_pose_tail_live(uint32_t matrices) {
    JakeAffine head, tail;
    int k;
    jake_read_affine(jake_part_matrix(matrices, JAKE_HEAD_PART), &head);
    for (k = 0; k < 2; ++k) {
        jake_multiply(&head, &g_tail_rel[k], &tail);
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
/* Spike's mesh header (colours, polys, parts, verts, ...) wherever it was
 * loaded: in levels at 0x801030BC, on the title screen elsewhere. */
static const uint8_t SPIKE_HEADER[12] = {
    0x1C, 0x00, 0x68, 0x01, 0x10, 0x00, 0x48, 0x01, 0x50, 0x00, 0x01, 0x00
};
static int jake_is_spike_header(uint32_t header) {
    uint32_t i;
    if (header < 0x80000000u || header >= 0x80200000u)
        return 0;
    for (i = 0; i < 10u; ++i)      /* +10 differs in the cutscenes' copies */
        if (psx_mod_read_byte(header + i) != SPIKE_HEADER[i])
            return 0;
    return 1;
}

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

/*
 * The swap is undone at DrawOTag, so between a frame's transform and its
 * DrawOTag each object holds the other's mesh. A savestate taken in that
 * window brings it back that way, with nothing left to undo it: Spike would
 * keep Jake's 18-part mesh without the tail being posed (or the race's Jake
 * Spike's). Seen at the transform outside our own swap, it is put back first.
 */
static uint32_t g_jake_object, g_jake_own[JAKE_MESH_WORDS];
/* A scene's own Spike (title, cutscenes) and when each side was last seen:
 * a pair seen within a few DrawOTags is in the same scene (cutscenes draw
 * two tables a frame). */
enum { JAKE_SCENE_FRAMES = 4 };
static uint32_t g_other_spike_object, g_other_spike[JAKE_MESH_WORDS];
static uint32_t g_other_spike_frame = 0x80000000u, g_jake_own_frame = 0x80000000u;
/* Spike's model is resident at the same place in every level; used when a
 * stuck state is loaded before his own mesh words have been seen. */
static const uint32_t SPIKE_MESH_RESIDENT[JAKE_MESH_WORDS] = {
    0x801030BCu, 0x801030C8u, 0x80104748u, 0x80103208u, 0x801032C8u
};

static void jake_unstick(uint32_t object) {
    const uint32_t word0 = psx_mod_read_word(object);
    int i;
    if (object == JAKE_SPIKE_OBJECT && !g_have_spike && g_jake_mesh_ready &&
        jake_is_our_mesh(word0) &&
        jake_part_count(SPIKE_MESH_RESIDENT[0]) == JAKE_SPIKE_PARTS) {
        for (i = 0; i < JAKE_MESH_WORDS; ++i)
            g_spike_mesh[i] = SPIKE_MESH_RESIDENT[i];
        g_have_spike = 1u;
    }
    if (object == JAKE_SPIKE_OBJECT && g_have_spike && g_jake_mesh_ready &&
        jake_is_our_mesh(word0)) {
        for (i = 0; i < JAKE_MESH_WORDS; ++i)
            psx_mod_write_word(object + (uint32_t)i * 4u, g_spike_mesh[i]);
    } else if (object == g_jake_object &&
               ((g_have_spike && word0 == g_spike_mesh[0]) ||
                (g_other_spike[0] != 0u && word0 == g_other_spike[0]))) {
        for (i = 0; i < JAKE_MESH_WORDS; ++i)
            psx_mod_write_word(object + (uint32_t)i * 4u, g_jake_own[i]);
    } else if (object == g_other_spike_object && g_jake_own[0] != 0u && word0 == g_jake_own[0]) {
        for (i = 0; i < JAKE_MESH_WORDS; ++i)
            psx_mod_write_word(object + (uint32_t)i * 4u, g_other_spike[i]);
    }
}

static void jake_transform_entry(struct CPUState* cpu, uint32_t address) {
    const uint32_t object = cpu->gpr[4];
    uint32_t header, matrices;
    (void)address;

    if (!g_enabled || jake_is_swapped(object))
        return;
    if (cpu->gpr[31] == JAKE_NET_TRANSFORM_RETURN) {
        jake_purple_net(object);
        return;
    }
    jake_unstick(object);
    header = psx_mod_read_word(object);
    matrices = psx_mod_read_word(object + 0x14u);
    if (object == JAKE_SPIKE_OBJECT) {
        g_level_spike_frame = g_frame;
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
    /*
     * Another Spike: the title screen's, or a cutscene's (the cutscenes load
     * their own copies of the cast). With Jake in the same scene the two
     * trade meshes - their textures are loaded for it - and Spike gets
     * Jake's live tail pose; alone he gets Jake's mesh from the disc, tail at
     * rest. One left holding our copy by a savestate is still Jake.
     */
    if (jake_is_spike_header(header) || (g_jake_mesh_ready && jake_is_our_mesh(header))) {
        if (!jake_is_our_mesh(header)) {
            g_other_spike_object = object;
            jake_read_mesh(object, g_other_spike);
            g_other_spike_frame = g_frame;
        }
        if (g_look == JAKE_NORMAL && g_frame - g_jake_own_frame <= JAKE_SCENE_FRAMES &&
            jake_is_jake_mesh(g_jake_own[0], g_jake_own[3])) {
            if (g_have_tail && g_frame - g_tail_frame <= JAKE_SCENE_FRAMES)
                jake_pose_tail_live(matrices);
            else
                jake_pose_tail_rest(matrices);
            jake_swap_in(object, g_jake_own);
            return;
        }
        jake_prepare_mesh(object);
        if (!g_jake_mesh_ready)
            return;
        if (g_have_tail && g_frame - g_tail_frame <= JAKE_SCENE_FRAMES)
            jake_pose_tail_live(matrices);
        else
            jake_pose_tail_rest(matrices);
        if (!jake_is_our_mesh(header))
            jake_swap_in(object, g_jake_mesh);
        return;
    }
    /* Jake himself (his race, cutscenes): he wears Spike - the scene's own
     * Spike when there is one, else the level's. */
    if (!jake_is_jake_mesh(header, psx_mod_read_word(object + 0x0Cu)))
        return;
    jake_capture_tail(matrices);
    g_jake_object = object;
    jake_read_mesh(object, g_jake_own);
    g_jake_own_frame = g_frame;
    if (g_frame - g_other_spike_frame <= JAKE_SCENE_FRAMES && jake_is_spike_header(g_other_spike[0]))
        jake_swap_in(object, g_other_spike);
    else if (g_have_spike)
        jake_swap_in(object, g_spike_mesh);
}

static int jake_is_life_model(uint32_t entry) {
    const uint32_t header = psx_mod_read_word(entry);
    const uint32_t verts = psx_mod_read_word(entry + 12u);
    int i, k;
    if ((header >> 24) != 0x80u || (verts >> 24) != 0x80u ||
        psx_mod_read_half(header + 2u) != JAKE_LIFE_POLYS ||
        psx_mod_read_half(header + 6u) != JAKE_LIFE_VERTS ||
        psx_mod_read_half(header + 8u) != JAKE_LIFE_COLOURS)
        return 0;
    for (i = 0; i < 3; ++i)
        for (k = 0; k < 3; ++k)
            if ((int16_t)psx_mod_read_half(verts + (uint32_t)JAKE_LIFE_KEY[i][0] * 8u +
                                           (uint32_t)k * 2u) != JAKE_LIFE_KEY[i][k + 1])
                return 0;
    return 1;
}

/* Find the extra-life model in this level's table (it moves between levels)
 * and patch it; a patched one passes the check too, and patching is
 * idempotent. */
static void jake_patch_life(void) {
    static uint32_t found, scans;
    const uint16_t clut = (uint16_t)((JAKE_LIFE_CLUT_Y << 6) | (JAKE_LIFE_CLUT_X >> 4));
    uint32_t entry, polys, verts;
    int i, k;
    if (found == 0u || !jake_is_life_model(found)) {
        found = 0u;
        if (scans++ % JAKE_LIFE_SCAN_PERIOD != 0u)
            return;
        for (entry = JAKE_LIFE_TABLE; entry < JAKE_LIFE_TABLE_END && found == 0u; entry += 16u)
            if (jake_is_life_model(entry))
                found = entry;
        if (found == 0u)
            return;
    }
    polys = psx_mod_read_word(found + 8u);
    verts = psx_mod_read_word(found + 12u);
    if (psx_mod_read_half(polys + 14u) == clut)
        return;
    for (i = 0; i < JAKE_LIFE_POLYS; ++i)
        psx_mod_write_half(polys + (uint32_t)i * JAKE_LIFE_POLY_SIZE + 14u, clut);
    for (i = 0; i < 8; ++i)
        for (k = 0; k < 3; ++k)
            psx_mod_write_half(verts + JAKE_LIFE_FOLD[i][0] * 8u + (uint32_t)k * 2u,
                               psx_mod_read_half(verts + JAKE_LIFE_FOLD[i][1] * 8u +
                                                 (uint32_t)k * 2u));
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
                if ((page & 0x1FFu) == icon->tpage &&
                    (icon->uv == JAKE_ANY_UV || (uv_word & 0xFFFFu) == icon->uv) &&
                    (uv_word >> 16) == icon->stock_clut) {
                    const uint32_t clut = icon->clut_x != 0u
                        ? ((uint32_t)icon->clut_y << 6) | (icon->clut_x >> 4u)
                        : icon->stock_clut;
                    const uint32_t uv = icon->new_uv == JAKE_ANY_UV ? (uv_word & 0xFFFFu) : icon->new_uv;
                    psx_mod_write_word(addr + 4u + (k + 2u) * 4u, (clut << 16) | uv);
                    break;   /* the swapped uv must not match the other entry */
                }
            }
        }
        at = tag & 0x00FFFFFFu;
    }
}

/*
 * "JAKE EDITION" under the title logo: a 256x62 4-bit texture (made by
 * modding/art/edition_tex.py in the logo's style) kept uploaded to VRAM that
 * is free on the title screen (x 960..1023, y 384..445; palette at
 * (960,448)) and drawn as one raw-textured quad linked at the near end of
 * the table, so it sits on the scene, under the menu. The game runs 384
 * pixels across a 4:3 picture, so the quad is 1.2x wider than the art.
 * The title screen is told by its code being loaded (two calls in it).
 */
#include "ape_jake_title_atlas.inc"
enum {
    ATLAS_VRAM_X = 960, ATLAS_VRAM_Y = 384,             /* 4-bit, rows 384..493 */
    ATLAS_CLUT_Y = 494,                                 /* palette at (960, 494) */
    ATLAS_ROWS = 7,                                     /* texel rows per copy packet */
    EDITION_X = 201, EDITION_Y = 138,                   /* screen boxes (384-wide picture) */
    EDITION_W = 108, EDITION_H = 22,
    LABEL_CENTRE = 338, LABEL_Y = 27, LABEL_H = 15,          /* the look, top right */
    LABEL_RIGHT = 380,                                       /* ...kept left of this */
    HINT_Y = 9, HINT_H = 16,                                 /* PRESS L1 / TO SWAP above it */
    CREDIT_RIGHT = 381, CREDIT_Y = 216, CREDIT_H = 22,       /* Mod By Flux, bottom right */
    TITLE_QUADS = 4
};
static uint32_t g_title_quads[2];
static uint8_t g_force_upload;
/* The attract demo plays a level with the title code still loaded; the
 * level's own Spike (never drawn on the title) gives it away. */
enum { EDITION_LEVEL_FRAMES = 60 };

static int jake_on_title(void) {
    return psx_mod_read_word(0x80136930u) == 0x0C005870u &&   /* jal 0x800161C0 */
           psx_mod_read_word(0x80136C94u) == 0x0C04DB40u &&   /* jal 0x80136D00 */
           g_frame - g_level_spike_frame > EDITION_LEVEL_FRAMES;
}

static void jake_upload_title(uint32_t table) {
    const uint32_t far_entry = table + 0x3FFCu;
    const uint32_t row_words = TITLE_ATLAS_W / 8u;      /* 4 texels a halfword */
    uint32_t words[3 + ATLAS_ROWS * TITLE_ATLAS_W / 8];
    uint32_t row, k, p;
    for (p = 0; p < TITLE_PALETTES; ++p) {
        words[0] = 0xA0000000u;
        words[1] = ((uint32_t)ATLAS_CLUT_Y << 16) | (ATLAS_VRAM_X + p * 16u);
        words[2] = (1u << 16) | 16u;
        for (k = 0; k < 16; k += 2)
            words[3 + k / 2] = TITLE_ATLAS_PALETTES[p][k] |
                               ((uint32_t)TITLE_ATLAS_PALETTES[p][k + 1] << 16);
        jake_link_packet(far_entry, words, 3u + 8u);
    }
    for (row = 0; row < TITLE_ATLAS_H; row += ATLAS_ROWS) {
        const uint32_t rows = TITLE_ATLAS_H - row < ATLAS_ROWS ? TITLE_ATLAS_H - row : ATLAS_ROWS;
        const uint16_t* src = TITLE_ATLAS_TEXELS + row * (TITLE_ATLAS_W / 4u);
        words[0] = 0xA0000000u;
        words[1] = ((uint32_t)(ATLAS_VRAM_Y + row) << 16) | ATLAS_VRAM_X;
        words[2] = (rows << 16) | (TITLE_ATLAS_W / 4u);
        for (k = 0; k < rows * row_words; ++k)
            words[3 + k] = src[2 * k] | ((uint32_t)src[2 * k + 1] << 16);
        jake_link_packet(far_entry, words, 3u + rows * row_words);
    }
}

/* One raw-textured quad (tag + 9 words) from an atlas rectangle. */
static void jake_title_quad(uint32_t packet, const TitleRect* r, uint32_t palette,
                            int32_t x0, int32_t y0, int32_t w, int32_t h) {
    const uint32_t clut = ((uint32_t)ATLAS_CLUT_Y << 6) | ((ATLAS_VRAM_X + palette * 16u) >> 4);
    const uint32_t page = (ATLAS_VRAM_X / 64u) | ((ATLAS_VRAM_Y / 256u) << 4);
    const uint32_t v0 = ATLAS_VRAM_Y % 256u + r->v, v1 = v0 + r->h - 1u;
    const uint32_t u0 = r->u, u1 = r->u + r->w - 1u;
    const uint32_t x1 = (uint32_t)(x0 + w) & 0xFFFFu, y1 = (uint32_t)(y0 + h) & 0xFFFFu;
    const uint32_t xa = (uint32_t)x0 & 0xFFFFu, ya = (uint32_t)y0 & 0xFFFFu;
    psx_mod_write_word(packet + 4u, 0x2D808080u);       /* quad, textured, raw */
    psx_mod_write_word(packet + 8u, (ya << 16) | xa);
    psx_mod_write_word(packet + 12u, (clut << 16) | (v0 << 8) | u0);
    psx_mod_write_word(packet + 16u, (ya << 16) | x1);
    psx_mod_write_word(packet + 20u, (page << 16) | (v0 << 8) | u1);
    psx_mod_write_word(packet + 24u, (y1 << 16) | xa);
    psx_mod_write_word(packet + 28u, (v1 << 8) | u0);
    psx_mod_write_word(packet + 32u, (y1 << 16) | x1);
    psx_mod_write_word(packet + 36u, (v1 << 8) | u1);
}

/* "JAKE EDITION" under the logo; top right, the look in use (NORMAL /
 * BRAINWASHED) under a small "PRESS L1 / TO SWAP"; bottom right, the "Mod By Flux
 * / x.com/F1ux0r" credit. Linked
 * after OT entry 0: on the scene, under the menu. The picture is 384
 * pixels across a 4:3 screen, so art is drawn 1.2x wide. */
static void jake_draw_title(uint32_t table, uint32_t index) {
    const int normal = g_look == JAKE_NORMAL;
    const TitleRect* label = normal ? &TITLE_NORMAL : &TITLE_EVIL;
    const int32_t label_w = (int32_t)(label->w * LABEL_H * 12 / (label->h * 10));
    const int32_t hint_w = (int32_t)(TITLE_HINT.w * HINT_H * 12 / (TITLE_HINT.h * 10));
    const int32_t credit_w = (int32_t)(TITLE_CREDIT.w * CREDIT_H * 12 / (TITLE_CREDIT.h * 10));
    /* BRAINWASHED is wide: the block moves left to stay on screen */
    const int32_t centre = LABEL_CENTRE + label_w / 2 > LABEL_RIGHT ? LABEL_RIGHT - label_w / 2
                                                                     : LABEL_CENTRE;
    uint32_t packet, old, q;
    if (g_title_quads[index] == 0u)
        g_title_quads[index] = psx_mod_alloc_gpu_dma_memory(TITLE_QUADS * 40u, 16u);
    packet = g_title_quads[index];
    if (packet == 0u)
        return;
    jake_title_quad(packet, &TITLE_EDITION, TITLE_TEXT_PALETTE,
                    EDITION_X, EDITION_Y, EDITION_W, EDITION_H);
    jake_title_quad(packet + 40u, label, TITLE_TEXT_PALETTE,
                    centre - label_w / 2, LABEL_Y, label_w, LABEL_H);
    jake_title_quad(packet + 80u, &TITLE_HINT, TITLE_TEXT_PALETTE,
                    centre - hint_w / 2, HINT_Y, hint_w, HINT_H);
    jake_title_quad(packet + 120u, &TITLE_CREDIT, TITLE_TEXT_PALETTE,
                    CREDIT_RIGHT - credit_w, CREDIT_Y, credit_w, CREDIT_H);
    old = psx_mod_read_word(table);                     /* entry 0: drawn last */
    for (q = TITLE_QUADS; q-- > 0u;) {
        const uint32_t at = packet + q * 40u;
        psx_mod_write_word(at, (9u << 24) | (old & 0x00FFFFFFu));
        old = (old & 0xFF000000u) | (at & 0x00FFFFFFu);
    }
    psx_mod_write_word(table, old);
}

/* ---- The look toggle --------------------------------------------------- */

/* Pad 1 as the driver left it: +2 buttons, active low; L1 = bit 10. */
static const uint32_t JAKE_PAD1_BUTTONS = 0x800E52D2u;
enum { JAKE_PAD_L1 = 1 << 10 };

/*
 * A switch click: the title menu's cursor sample (SPU 0x2BFC0, pitch
 * 0x344), a little higher for NORMAL and lower for EVIL, on a voice the
 * sound driver has free, claimed in its voice record (0x800E07F0, 0x66
 * each: +0 busy, +0x1A no-steal) until it has played.
 */
static const uint32_t SPU_VOICE_BASE = 0x1F801C00u, SPU_KEY_ON_LOW = 0x1F801D88u;
static const uint32_t SND_VOICE_RECORDS = 0x800E07F0u;
enum { SPU_VOICES = 24, SND_RECORD_SIZE = 0x66, CLICK_SAMPLE = 0x2BFC0, CLICK_FRAMES = 30 };
static int g_click_voice = -1;
static uint32_t g_click_frames;

static void jake_release_click(void) {
    if (g_click_voice >= 0) {
        const uint32_t record = SND_VOICE_RECORDS + (uint32_t)g_click_voice * SND_RECORD_SIZE;
        if (psx_mod_read_half(record + 0x1Au) == 1u) {
            psx_mod_write_half(record + 0x1Au, 0u);
            psx_mod_write_half(record + 0x00u, 0u);
        }
    }
    g_click_voice = -1;
}

static void jake_play_click(uint16_t pitch) {
    int voice;
    uint32_t base;
    jake_release_click();
    for (voice = SPU_VOICES - 1; voice >= 0; --voice)
        if (psx_mod_read_half(SND_VOICE_RECORDS + (uint32_t)voice * SND_RECORD_SIZE) == 0u &&
            psx_mod_read_half(SPU_VOICE_BASE + (uint32_t)voice * 0x10u + 0xCu) == 0u)
            break;
    if (voice < 0)
        return;
    psx_mod_write_half(SND_VOICE_RECORDS + (uint32_t)voice * SND_RECORD_SIZE + 0x00u, 1u);
    psx_mod_write_half(SND_VOICE_RECORDS + (uint32_t)voice * SND_RECORD_SIZE + 0x1Au, 1u);
    g_click_voice = voice;
    g_click_frames = 0u;
    base = SPU_VOICE_BASE + (uint32_t)voice * 0x10u;
    psx_mod_write_half(base + 0x0u, 0x290Au);
    psx_mod_write_half(base + 0x2u, 0x290Au);
    psx_mod_write_half(base + 0x4u, pitch);
    psx_mod_write_half(base + 0x6u, (uint16_t)(CLICK_SAMPLE >> 3));
    psx_mod_write_half(base + 0x8u, 0x80FFu);
    psx_mod_write_half(base + 0xAu, 0x5FC0u);
    psx_mod_write_half(SPU_KEY_ON_LOW + (voice >= 16 ? 2u : 0u), (uint16_t)(1u << (voice & 15)));
}

/* Called every VBlank: give the voice back once the click has played. */
static void jake_click_vblank(void) {
    if (g_click_voice >= 0 && ++g_click_frames > CLICK_FRAMES)
        jake_release_click();
}

/* On the title: L1 (pressed, not held) switches the look. */
static void jake_title_input(void) {
    static uint16_t was;
    const uint16_t pressed = (uint16_t)~psx_mod_read_half(JAKE_PAD1_BUTTONS);
    if ((pressed & JAKE_PAD_L1) && !(was & JAKE_PAD_L1)) {
        const int next = g_look == JAKE_NORMAL ? JAKE_EVIL : JAKE_NORMAL;
        if (g_look_built[next]) {
            jake_select_look(next);
            jake_play_click(next == JAKE_NORMAL ? 0x3E0u : 0x2C0u);
            g_force_upload = 1u;
        }
    }
    was = pressed;
}

/* ---- Per-save look ----------------------------------------------------- */

/*
 * Every boot starts as NORMAL and L1 on the title switches; each save file
 * then remembers the look it was saved with, in mods/play-as-jake-saves.cfg
 * (nothing is added to the save itself). A save is named by a hash of its
 * whole slot: the game's card file BASCUS-94423SYS holds 4 slots of 0x7A0
 * bytes from +0x180 (an empty slot starts 0xFFFF).
 *  - Saving: the card files are checked about once a second; every slot
 *    whose contents changed gets the current look. Only complete reads that
 *    hold the game's file count (the emulator rewrites the card file now and
 *    then, e.g. on savestate loads, and a half-written read must not look
 *    like new saves); a save that changes nothing records nothing.
 *  - The load / save file screen (flag 0x800E3880 = 1, file cursor 0..3 at
 *    0x800E3886) keeps the card's block in RAM at 0x800E1880, exactly as
 *    on the card. While it is up, the file under the cursor is remembered
 *    (labels under each picture: branch file-labels-wip, they flickered); when the progress in RAM (0x800DF81C) then
 *    changes (the game copies the save in while this screen is still up),
 *    a file was loaded and its look is put back on. Going back to
 *    the title forgets it.
 */
static const char JAKE_SAVES_FILE[] = "mods/play-as-jake-saves.cfg";
static const char JAKE_CARD_FILE_NAME[] = "BASCUS-94423SYS";
static const uint32_t JAKE_PROGRESS_RAM = 0x800DF81Cu;
static const uint32_t JAKE_FILE_SCREEN = 0x800E3880u;
static const uint32_t JAKE_FILE_CURSOR = 0x800E3886u;
static const uint32_t JAKE_FILE_BLOCK = 0x800E1880u;
enum {
    JAKE_CARDS = 2, JAKE_CARD_SIZE = 0x20000, JAKE_CARD_BLOCK = 0x2000,
    JAKE_SAVE_SLOTS = 4, JAKE_SLOT_FIRST = 0x180, JAKE_SLOT_SIZE = 0x7A0,
    JAKE_PROGRESS_LEN = 0x2F0,
    JAKE_MAX_SAVES = 128, JAKE_CARD_CHECK = 60, JAKE_RAM_CHECK = 10
};
static struct { uint64_t hash; uint8_t look; } g_saves[JAKE_MAX_SAVES];
static int g_save_count;
static char g_card_path[JAKE_CARDS][512];
static uint64_t g_slot_hash[JAKE_CARDS][JAKE_SAVE_SLOTS];
static long long g_card_stamp[JAKE_CARDS];
static uint8_t g_card_seen[JAKE_CARDS];
static uint64_t g_screen_hash[JAKE_SAVE_SLOTS];   /* files on the file screen */
static uint8_t g_file_screen;

static uint64_t jake_hash(const uint8_t* p, uint32_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    uint32_t i;
    for (i = 0; i < n; ++i)
        h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

/* A slot's name, or 0 if it is empty. */
static uint64_t jake_slot_hash(const uint8_t* slot) {
    if (slot[0] == 0xFFu && slot[1] == 0xFFu)
        return 0u;
    return jake_hash(slot, JAKE_SLOT_SIZE);
}

static int jake_save_look(uint64_t hash) {
    int i;
    for (i = 0; hash != 0u && i < g_save_count; ++i)
        if (g_saves[i].hash == hash)
            return g_saves[i].look;
    return -1;
}

static void jake_save_looks_write(void) {
    FILE* f = fopen(JAKE_SAVES_FILE, "wb");
    int i;
    if (!f)
        return;
    fputs("# Play as Jake: the look each save was made with (hash of its save slot)\n", f);
    for (i = 0; i < g_save_count; ++i)
        fprintf(f, "%016llx %s\n", (unsigned long long)g_saves[i].hash,
                g_saves[i].look == JAKE_NORMAL ? "normal" : "brainwashed");
    fclose(f);
}

static void jake_save_looks_read(void) {
    char line[128];
    FILE* f = fopen(JAKE_SAVES_FILE, "rb");
    if (!f)
        return;
    while (g_save_count < JAKE_MAX_SAVES && fgets(line, sizeof(line), f)) {
        unsigned long long hash;
        char word[32];
        if (line[0] == '#' || sscanf(line, "%llx %31s", &hash, word) != 2)
            continue;
        g_saves[g_save_count].hash = hash;
        g_saves[g_save_count].look = strcmp(word, "normal") == 0 ? JAKE_NORMAL : JAKE_EVIL;
        ++g_save_count;
    }
    fclose(f);
}

static void jake_save_looks_record(uint64_t hash, int look) {
    int i = 0;
    while (i < g_save_count && g_saves[i].hash != hash)
        ++i;
    if (i == g_save_count) {
        if (g_save_count == JAKE_MAX_SAVES) {   /* forget the oldest */
            memmove(g_saves, g_saves + 1, sizeof(g_saves[0]) * (JAKE_MAX_SAVES - 1));
            --g_save_count;
            i = g_save_count;
        }
        ++g_save_count;
    }
    g_saves[i].hash = hash;
    g_saves[i].look = (uint8_t)look;
}

/* The card paths from settings.toml ([memcard] card1 / card2). */
static void jake_find_cards(void) {
    char line[600];
    FILE* f = fopen("settings.toml", "rb");
    snprintf(g_card_path[0], sizeof(g_card_path[0]), "saves/card1.mcd");
    snprintf(g_card_path[1], sizeof(g_card_path[1]), "saves/card2.mcd");
    if (!f)
        return;
    while (fgets(line, sizeof(line), f)) {
        const int card = strncmp(line, "card1", 5) == 0 ? 0 : strncmp(line, "card2", 5) == 0 ? 1 : -1;
        char* q0 = strchr(line, '"');
        char* q1 = q0 ? strchr(q0 + 1, '"') : NULL;
        if (card >= 0 && q1) {
            *q1 = '\0';
            snprintf(g_card_path[card], sizeof(g_card_path[card]), "%s", q0 + 1);
        }
    }
    fclose(f);
}

/* Slot hashes of a card file (0 = empty slot); 0 if the file could not be
 * read whole or has no Ape Escape save on it. */
static int jake_card_hashes(int card, uint64_t* hashes) {
    static uint8_t data[JAKE_CARD_SIZE];
    FILE* f = fopen(g_card_path[card], "rb");
    int frame, slot;
    memset(hashes, 0, sizeof(uint64_t) * JAKE_SAVE_SLOTS);
    if (!f)
        return 0;
    if (fread(data, 1, sizeof(data), f) != sizeof(data)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    for (frame = 1; frame < 16; ++frame) {
        const uint8_t* dir = data + frame * 128;
        if ((dir[0] & 0xF0u) == 0x50u &&
            strncmp((const char*)dir + 10, JAKE_CARD_FILE_NAME, sizeof(JAKE_CARD_FILE_NAME)) == 0) {
            for (slot = 0; slot < JAKE_SAVE_SLOTS; ++slot)
                hashes[slot] = jake_slot_hash(data + frame * JAKE_CARD_BLOCK + JAKE_SLOT_FIRST +
                                              slot * JAKE_SLOT_SIZE);
            return 1;
        }
    }
    return 0;
}

static uint64_t jake_ram_hash(uint32_t addr, uint32_t len, int as_slot) {
    static uint8_t buf[JAKE_SLOT_SIZE];
    uint32_t i;
    for (i = 0; i < len; ++i)
        buf[i] = psx_mod_read_byte(addr + i);
    return as_slot ? jake_slot_hash(buf) : jake_hash(buf, len);
}

static void jake_save_looks_tick(void) {
    static uint32_t frames;
    static uint64_t last_progress, pending;
    int card, slot, changed = 0;
    const uint8_t cursor = psx_mod_read_byte(JAKE_FILE_CURSOR);
    g_file_screen = (uint8_t)(psx_mod_read_byte(JAKE_FILE_SCREEN) == 1u &&
                              psx_mod_read_byte(JAKE_FILE_BLOCK) == 'S' &&
                              psx_mod_read_byte(JAKE_FILE_BLOCK + 1u) == 'C');
    if (g_file_screen && frames % JAKE_RAM_CHECK == 0u) {
        for (slot = 0; slot < JAKE_SAVE_SLOTS; ++slot)
            g_screen_hash[slot] = jake_ram_hash(JAKE_FILE_BLOCK + JAKE_SLOT_FIRST +
                                                (uint32_t)slot * JAKE_SLOT_SIZE, JAKE_SLOT_SIZE, 1);
        if (cursor < JAKE_SAVE_SLOTS)
            pending = g_screen_hash[cursor];
    }
    if (jake_on_title())
        pending = 0u;
    if (frames % JAKE_RAM_CHECK == 0u) {
        const uint64_t progress = jake_ram_hash(JAKE_PROGRESS_RAM, JAKE_PROGRESS_LEN, 0);
        if (progress != last_progress && pending != 0u) {   /* loaded (copied while the screen is still up) */
            const int look = jake_save_look(pending);
            if (look >= 0 && look != g_look && g_look_built[look]) {
                jake_select_look(look);
                g_force_upload = 1u;
            }
            pending = 0u;
        }
        last_progress = progress;
    }
    if (frames++ % JAKE_CARD_CHECK != 0u)
        return;
    for (card = 0; card < JAKE_CARDS; ++card) {
        struct stat st;
        uint64_t hashes[JAKE_SAVE_SLOTS];
        long long stamp;
        if (stat(g_card_path[card], &st) != 0)
            continue;
        stamp = (long long)st.st_mtime * 1000003LL + (long long)st.st_size;
        if (g_card_seen[card] && stamp == g_card_stamp[card])
            continue;
        if (!jake_card_hashes(card, hashes))
            continue;   /* try again next check */
        for (slot = 0; g_card_seen[card] && slot < JAKE_SAVE_SLOTS; ++slot)
            if (hashes[slot] != 0u && hashes[slot] != g_slot_hash[card][slot]) {
                jake_save_looks_record(hashes[slot], g_look);
                changed = 1;
            }
        memcpy(g_slot_hash[card], hashes, sizeof(hashes));
        g_card_stamp[card] = stamp;
        g_card_seen[card] = 1u;
    }
    if (changed)
        jake_save_looks_write();
}

/* The frame's drawing is queued: give every swapped object its own mesh
 * back before anything animates it again. */
static void jake_draw_ot_entry(struct CPUState* cpu, uint32_t address) {
    static uint32_t uploads;
    uint32_t index;
    int i, k;
    (void)address;
    jake_purple_trail();
    if (g_enabled && g_jake_mesh_ready)
        jake_save_looks_tick();
    /* Keep the face in VRAM (level loads may write over that strip). */
    for (index = 0u; index < 2u; ++index) {
        const uint32_t table = JAKE_ORDERING_TABLE + JAKE_ORDERING_TABLE_STRIDE * index;
        if (g_enabled && g_jake_mesh_ready && cpu->gpr[4] == table + 0x3FFCu &&
            (uploads++ % JAKE_UPLOAD_PERIOD == 0u || g_force_upload)) {
            g_force_upload = 0u;
            if (g_arena[index] == 0u)
                g_arena[index] = psx_mod_alloc_gpu_dma_memory(JAKE_ARENA_SIZE, 16u);
            g_arena_index = (int)index;
            g_arena_used = 0u;
            jake_upload_face(table);
            if (jake_on_title())
                jake_upload_title(table);
        }
        if (g_enabled && g_jake_mesh_ready && cpu->gpr[4] == table + 0x3FFCu) {
            jake_recolour_hud(table);
            if (jake_on_title()) {
                jake_title_input();
                jake_draw_title(table, index);
            }
        }
    }
    if (g_enabled && g_jake_mesh_ready)
        jake_patch_life();
    for (i = 0; i < g_swapped_count; ++i)
        for (k = 0; k < JAKE_MESH_WORDS; ++k)
            psx_mod_write_word(g_swapped[i] + (uint32_t)k * 4u, g_swapped_words[i][k]);
    g_swapped_count = 0;
    ++g_frame;
}

static void jake_load_voice(const JakeDiscFile* dat) {
    static uint8_t disc[JAKE_VOICE_SIZE];
    static int16_t ouch[JAKE_OUCH_SAMPLES];
    /* The 'ouch' is XA audio (Form 2): raw sectors, not a file read. */
    const int have_ouch = jake_load_ouch(ouch);
    int y, ok = jake_disc_piece(dat, JAKE_DISC_VOICE, disc, JAKE_VOICE_SIZE) && disc[17] == 0x04u;
    for (y = 0; ok && y < JAKE_YELL_COUNT; ++y)
        ok = jake_disc_piece(dat, g_yells[y].disc, g_yells[y].head, sizeof(g_yells[y].head)) &&
             g_yells[y].head[17] == 0x04u;
    if (ok)
        jake_build_voice(disc, have_ouch ? ouch : NULL);
    else
        fprintf(stderr, "play-as-jake: Jake's voice was not found on this disc\n");
    if (ok && !have_ouch)
        fprintf(stderr, "play-as-jake: Jake's 'ouch' was not found on this disc; hurt cry silenced\n");
}

/* Disc data is read on the first frame, from the emulation thread (where the
 * runtime's disc reads are allowed), rather than at activation. */
static void jake_load_from_disc(void) {
    JakeDiscFile dat = jake_disc_file(JAKE_DAT_FILE);
    if (!dat.data)
        fprintf(stderr, "play-as-jake: %s could not be read from the disc\n", JAKE_DAT_FILE);
    jake_load_disc(&dat);
    jake_load_voice(&dat);
    free(dat.data);
}

static void jake_activate(void) {
    g_enabled = 1u;
    jake_find_cards();
    jake_save_looks_read();
}

PSX_MOD_CONSTRUCTOR(ape_register_play_as_jake_plugin) {
    (void)psx_mod_register_activation_plugin("ape.characters.play-as-jake", jake_activate);
    (void)psx_mod_register_vblank_plugin("ape.characters.play-as-jake", jake_vblank);
    (void)psx_mod_register_function_entry_plugin(
        "ape.characters.play-as-jake", JAKE_TRANSFORM_FUNCTION, jake_transform_entry);
    (void)psx_mod_register_function_entry_plugin(
        "ape.characters.play-as-jake", JAKE_DRAW_OT_FUNCTION, jake_draw_ot_entry);
    (void)psx_mod_register_function_entry_plugin(
        "ape.characters.play-as-jake", JAKE_GLOW_BEAM_FUNCTION, jake_glow_beam_entry);
    (void)psx_mod_register_function_entry_plugin(
        "ape.characters.play-as-jake", JAKE_TRAIL_FUNCTION, jake_trail_entry);
}
