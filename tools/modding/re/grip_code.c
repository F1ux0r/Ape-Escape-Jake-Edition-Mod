
/* ---- AE2 grip ----------------------------------------------------------- */

/*
 * The AE2 flyer is held by a grip block under a silver spindle, with a red
 * cap over the hub. The stock model has none of that, so it is drawn on top:
 * boxes in the flyer model's own space (blades in the y-z plane at x 12..17,
 * spinning about x, the hand toward -x), transformed by the flyer matrix the
 * handler draws with (0x800E9E40, camera space, captured per frame buffer).
 * Only the faces turned toward the camera are drawn.
 */
static const uint32_t APE_FLYER_MODEL_MATRIX = 0x800E9E40u;
static uint32_t g_flyer_render_tick;

static void ape_flyer_handler_entry(struct CPUState* cpu, uint32_t address) {
    ApeDrawFrame* f = &g_draw_frames[psx_mod_read_word(APE_FRAME_BUFFER_INDEX) & 1u];
    ApeAffine m;
    int i, j;
    (void)cpu;
    (void)address;
    ape_read_affine(APE_FLYER_MODEL_MATRIX, &m);
    for (i = 0; i < 3; ++i) {
        for (j = 0; j < 3; ++j)
            f->flyer_r[i][j] = m.r[i][j];
        f->flyer_t[i] = m.t[i];
    }
    f->flyer_valid = 1u;
    ++g_flyer_render_tick;
}

/* Model point -> screen through the frame's flyer matrix. */
static int ape_project_flyer(const ApeDrawFrame* f, const float m[3], int* sx, int* sy) {
    float v[3];
    int i;
    for (i = 0; i < 3; ++i)
        v[i] = f->flyer_t[i] + f->flyer_r[i][0] * m[0] + f->flyer_r[i][1] * m[1] +
               f->flyer_r[i][2] * m[2];
    if (v[2] < 32.0f)
        return 0;
    *sx = (int)((float)(f->ofx >> 16) + v[0] * (float)f->h / v[2]);
    *sy = (int)((float)(f->ofy >> 16) + v[1] * (float)f->h / v[2]);
    return 1;
}

static uint32_t ape_shade(uint32_t bgr, float k) {
    uint32_t out = 0u;
    int c;
    for (c = 0; c < 3; ++c) {
        float v = (float)((bgr >> (c * 8)) & 0xFFu) * k;
        if (v > 255.0f)
            v = 255.0f;
        out |= (uint32_t)v << (c * 8);
    }
    return out;
}

/* Axis-aligned box in model space; each face drawn only when it faces the
 * camera (screen winding), shaded per face so the block reads as solid. */
static void ape_draw_flyer_box(const ApeDrawFrame* f, const float lo[3], const float hi[3],
                               const uint32_t face_colour[6]) {
    static const int faces[6][4] = {
        { 0, 1, 2, 3 }, { 5, 4, 7, 6 },   /* -x, +x */
        { 4, 0, 6, 2 }, { 1, 5, 3, 7 },   /* -y, +y */
        { 4, 5, 0, 1 }, { 2, 3, 6, 7 }    /* -z, +z */
    };
    static const float light[6] = { 0.7f, 1.0f, 0.8f, 0.9f, 0.75f, 1.05f };
    int sx[8], sy[8], ok[8], c, i;
    for (c = 0; c < 8; ++c) {
        float p[3];
        p[0] = (c & 4) ? hi[0] : lo[0];
        p[1] = (c & 1) ? hi[1] : lo[1];
        p[2] = (c & 2) ? hi[2] : lo[2];
        ok[c] = ape_project_flyer(f, p, &sx[c], &sy[c]);
    }
    for (i = 0; i < 6; ++i) {
        const int* q = faces[i];
        int x[4], y[4], k;
        uint32_t cols[4];
        long area;
        if (!ok[q[0]] || !ok[q[1]] || !ok[q[2]] || !ok[q[3]])
            continue;
        /* corners in strip order: the face outline is 0,1,3,2 */
        area = (long)(sx[q[1]] - sx[q[0]]) * (sy[q[3]] - sy[q[0]]) -
               (long)(sx[q[3]] - sx[q[0]]) * (sy[q[1]] - sy[q[0]]);
        if (area * APE_FLYER_FACE_SIGN <= 0)
            continue;
        for (k = 0; k < 4; ++k) {
            x[k] = sx[q[k]];
            y[k] = sy[q[k]];
            cols[k] = ape_shade(face_colour[i], light[i] * (k < 2 ? 1.0f : 0.85f));
        }
        ape_gpu_gquad(x, y, cols);
    }
}

static void ape_draw_flyer_grip(void) {
    static uint32_t last_tick, stale;
    const ApeDrawFrame* f;
    uint32_t flip = APE_DRAW_BUFFER_FLIP;
    /* BGR colours */
    const uint32_t silver = 0xC8C4C0u, steel = 0x9C9894u, blue = 0xC86A3Cu,
                   orange = 0x2A80E8u, red = 0x2828D8u;
    if (g_flyer_render_tick != last_tick) {
        last_tick = g_flyer_render_tick;
        stale = 0u;
    } else if (++stale > 2u) {
        return;   /* flyer not drawn: paused, put away or a cutscene */
    }
    if (g_debug != 0u && psx_mod_read_word(g_debug + 140u) != 0u)
        flip = psx_mod_read_word(g_debug + 140u) & 1u;
    f = &g_draw_frames[(psx_mod_read_word(APE_FRAME_BUFFER_INDEX) & 1u) ^ flip];
    if (!f->flyer_valid || f->h == 0)
        return;
    {
        /* grip block in the hand: blue panels front and back, silver round */
        const float lo[3] = { APE_FLYER_GRIP_LOW, -9.0f, -18.0f };
        const float hi[3] = { APE_FLYER_GRIP_HIGH, 9.0f, 4.0f };
        const uint32_t cols[6] = { steel, silver, silver, silver, blue, blue };
        ape_draw_flyer_box(f, lo, hi, cols);
    }
    {
        /* orange band round the top of the grip */
        const float lo[3] = { APE_FLYER_GRIP_HIGH - 4.0f, -9.5f, -18.5f };
        const float hi[3] = { APE_FLYER_GRIP_HIGH, 9.5f, 4.5f };
        const uint32_t cols[6] = { orange, orange, orange, orange, orange, orange };
        ape_draw_flyer_box(f, lo, hi, cols);
    }
    {
        /* silver spindle up to the hub */
        const float lo[3] = { APE_FLYER_GRIP_HIGH, -3.0f, -10.0f };
        const float hi[3] = { 12.0f, 3.0f, -4.0f };
        const uint32_t cols[6] = { silver, silver, silver, steel, steel, silver };
        ape_draw_flyer_box(f, lo, hi, cols);
    }
    {
        /* red cap over the hub */
        const float lo[3] = { 17.0f, -4.0f, -11.0f };
        const float hi[3] = { 22.0f, 4.0f, -3.0f };
        const uint32_t cols[6] = { red, red, red, red, red, red };
        ape_draw_flyer_box(f, lo, hi, cols);
    }
}
