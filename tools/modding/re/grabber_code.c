/* ---- Grabber ------------------------------------------------------------ */

/*
 * The grabber: rope coiled around Spike's right wrist, a length of rope
 * hanging from the hand, and a three-pronged claw at its end. Thrown, the
 * rope runs from the hand to the flying claw. Everything is drawn with GPU
 * primitives after the frame (no depth test), in world space projected with
 * the game's own camera.
 */
static const float APE_GRABBER_HANG = 70.0f;       /* idle rope below hand */
static const float APE_COIL_RADIUS = 12.0f;
static const float APE_CLAW_SCALE = 1.0f;

static const uint32_t APE_ROPE_LIGHT = 0x3A78B0u;  /* BGR: warm tan */
static const uint32_t APE_ROPE_DARK = 0x1E4270u;
static const uint32_t APE_CLAW_LIGHT = 0xD8D0C8u;  /* steel */
static const uint32_t APE_CLAW_MID = 0x8C8680u;
static const uint32_t APE_CLAW_DARK = 0x4A4643u;

static void ape_gpu_gline(int x0, int y0, uint32_t c0, int x1, int y1, uint32_t c1) {
    psx_mod_write_word(PSX_GPU_GP0_ADDRESS, 0x50000000u | c0);
    psx_mod_write_word(PSX_GPU_GP0_ADDRESS, ((uint32_t)(uint16_t)y0 << 16u) | (uint16_t)x0);
    psx_mod_write_word(PSX_GPU_GP0_ADDRESS, c1);
    psx_mod_write_word(PSX_GPU_GP0_ADDRESS, ((uint32_t)(uint16_t)y1 << 16u) | (uint16_t)x1);
}

/* Gouraud quad; corners in strip order (0,1,2 and 1,2,3). */
static void ape_gpu_gquad(const int x[4], const int y[4], const uint32_t c[4]) {
    int i;
    for (i = 0; i < 4; ++i) {
        psx_mod_write_word(PSX_GPU_GP0_ADDRESS, (i == 0 ? 0x38000000u : 0u) | c[i]);
        psx_mod_write_word(PSX_GPU_GP0_ADDRESS, ((uint32_t)(uint16_t)y[i] << 16u) | (uint16_t)x[i]);
    }
}

/* A rope between two world points: three shaded strands a pixel apart. */
static void ape_draw_rope_segment(const float a[3], const float b[3]) {
    int x0, y0, x1, y1;
    if (!ape_project(a, &x0, &y0) || !ape_project(b, &x1, &y1))
        return;
    ape_gpu_gline(x0, y0, APE_ROPE_DARK, x1, y1, APE_ROPE_DARK);
    ape_gpu_gline(x0 + 1, y0, APE_ROPE_LIGHT, x1 + 1, y1, APE_ROPE_LIGHT);
    ape_gpu_gline(x0, y0 + 1, APE_ROPE_LIGHT, x1, y1 + 1, APE_ROPE_DARK);
}

static void ape_normalize3(float v[3]) {
    const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-4f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

static void ape_cross3(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

/* Two unit vectors perpendicular to axis. */
static void ape_basis(const float axis[3], float u[3], float v[3]) {
    const float ref[3] = { fabsf(axis[1]) < 0.9f ? 0.0f : 1.0f,
                           fabsf(axis[1]) < 0.9f ? 1.0f : 0.0f, 0.0f };
    ape_cross3(axis, ref, u);
    ape_normalize3(u);
    ape_cross3(axis, u, v);
    ape_normalize3(v);
}

/* Rope wrapped three times around the wrist, along the forearm. */
static void ape_draw_coil(const float hand[3], const float elbow[3]) {
    float axis[3], u[3], v[3];
    int loop, seg, k;
    for (k = 0; k < 3; ++k)
        axis[k] = hand[k] - elbow[k];
    ape_normalize3(axis);
    ape_basis(axis, u, v);
    for (loop = 0; loop < 3; ++loop) {
        float prev[3];
        const float back = -6.0f - 7.0f * (float)loop;
        for (seg = 0; seg <= 10; ++seg) {
            const float a = (float)seg / 10.0f * 6.2831853f;
            const float c = cosf(a), s = sinf(a);
            float p[3];
            for (k = 0; k < 3; ++k)
                p[k] = hand[k] + axis[k] * (back + 7.0f * (float)seg / 10.0f) +
                       (u[k] * c + v[k] * s) * APE_COIL_RADIUS;
            if (seg > 0)
                ape_draw_rope_segment(prev, p);
            for (k = 0; k < 3; ++k)
                prev[k] = p[k];
        }
    }
}

/* Claw at hub, pointing along dir: a hub and three curved prongs. */
static void ape_draw_claw(const float hub[3], const float dir_in[3], int closed, float spin) {
    float dir[3], u[3], v[3];
    const float sc = APE_CLAW_SCALE;
    const float r_mid = (closed ? 14.0f : 26.0f) * sc;
    const float r_tip = (closed ? 4.0f : 20.0f) * sc;
    int prong, k;

    for (k = 0; k < 3; ++k)
        dir[k] = dir_in[k];
    ape_normalize3(dir);
    ape_basis(dir, u, v);

    for (prong = 0; prong < 3; ++prong) {
        const float a = spin + (float)prong * 2.0943951f;
        const float c = cosf(a), s = sinf(a);
        float radial[3], tangent[3], pts[3][3], widths[3];
        int j;
        for (k = 0; k < 3; ++k) {
            radial[k] = u[k] * c + v[k] * s;
            tangent[k] = -u[k] * s + v[k] * c;
        }
        /* base on the hub, knuckle flared out, tip curled back in */
        for (k = 0; k < 3; ++k) {
            pts[0][k] = hub[k] + radial[k] * 7.0f * sc;
            pts[1][k] = hub[k] + dir[k] * 20.0f * sc + radial[k] * r_mid;
            pts[2][k] = hub[k] + dir[k] * 40.0f * sc + radial[k] * r_tip;
        }
        widths[0] = 5.0f * sc; widths[1] = 4.0f * sc; widths[2] = 1.0f * sc;
        for (j = 0; j < 2; ++j) {
            float corner[4][3];
            int x[4], y[4], ok = 1;
            const uint32_t cols[4] = { j == 0 ? APE_CLAW_LIGHT : APE_CLAW_MID,
                                       j == 0 ? APE_CLAW_MID : APE_CLAW_DARK,
                                       j == 0 ? APE_CLAW_MID : APE_CLAW_DARK,
                                       APE_CLAW_DARK };
            for (k = 0; k < 3; ++k) {
                corner[0][k] = pts[j][k] + tangent[k] * widths[j];
                corner[1][k] = pts[j][k] - tangent[k] * widths[j];
                corner[2][k] = pts[j + 1][k] + tangent[k] * widths[j + 1];
                corner[3][k] = pts[j + 1][k] - tangent[k] * widths[j + 1];
            }
            for (k = 0; k < 4; ++k)
                if (!ape_project(corner[k], &x[k], &y[k]))
                    ok = 0;
            if (ok)
                ape_gpu_gquad(x, y, cols);
        }
    }

    /* Hub: a small shaded block where the rope ties on. */
    {
        float corner[4][3];
        int x[4], y[4], ok = 1;
        const uint32_t cols[4] = { APE_CLAW_LIGHT, APE_CLAW_MID, APE_CLAW_MID, APE_CLAW_DARK };
        for (k = 0; k < 3; ++k) {
            corner[0][k] = hub[k] - dir[k] * 6.0f * sc + u[k] * 8.0f * sc;
            corner[1][k] = hub[k] - dir[k] * 6.0f * sc - u[k] * 8.0f * sc;
            corner[2][k] = hub[k] + dir[k] * 8.0f * sc + u[k] * 8.0f * sc;
            corner[3][k] = hub[k] + dir[k] * 8.0f * sc - u[k] * 8.0f * sc;
        }
        for (k = 0; k < 4; ++k)
            if (!ape_project(corner[k], &x[k], &y[k]))
                ok = 0;
        if (ok)
            ape_gpu_gquad(x, y, cols);
    }
}

static void ape_draw_net_extras(void) {
    float hand[3], elbow[3], hub[3], down[3] = { 0.0f, 1.0f, 0.0f };
    int k;
    if (!g_view.valid || !g_spike_hands.valid || !ape_net_is_held())
        return;
    for (k = 0; k < 3; ++k) {
        hand[k] = g_spike_hands.right_world[k];
        elbow[k] = g_spike_hands.right_elbow_world[k];
    }
    ape_draw_coil(hand, elbow);

    if (ape_grapple_away()) {
        const int closed = g_grapple.phase == APE_GRAPPLE_REEL ||
                           g_grapple.phase == APE_GRAPPLE_SWOOP;
        float back[3];
        /* Rope ties onto the back of the hub. */
        for (k = 0; k < 3; ++k)
            back[k] = g_grapple.pos[k] - g_grapple.dir[k] * 6.0f;
        ape_draw_rope_segment(hand, back);
        ape_draw_claw(g_grapple.pos, g_grapple.dir, closed, 0.0f);
    } else {
        /* Hanging from the hand, swaying a little. */
        const float t = (float)g_net_power.glow_frame * 0.08f;
        float sway[3];
        sway[0] = sinf(t) * 6.0f;
        sway[1] = 0.0f;
        sway[2] = cosf(t * 0.7f) * 4.0f;
        for (k = 0; k < 3; ++k)
            hub[k] = hand[k] + down[k] * APE_GRABBER_HANG + sway[k];
        ape_draw_rope_segment(hand, hub);
        {
            float dir[3];
            for (k = 0; k < 3; ++k)
                dir[k] = hub[k] - hand[k];
            ape_draw_claw(hub, dir, 0, t * 0.3f);
        }
    }
}

