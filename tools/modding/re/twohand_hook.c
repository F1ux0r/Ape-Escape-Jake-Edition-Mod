/* ---- Two-handed net ----------------------------------------------------- */

/*
 * Powered up, Spike holds the net head (handle removed) in his LEFT hand and
 * the grabber in his RIGHT. Spike's render object is his actor 0x800EC1E0;
 * its part 15 is the right hand and part 16 the left (camera space, found by
 * projecting each part onto the screen). The net is built on the right hand,
 * so each net part N is re-hung on the left hand as  L * R^-1 * N.
 *
 * The handle is vertices 28..59 of the net model's part 1 (the rim is 0..27
 * and 60..71); while powered they are collapsed onto the rim so they draw
 * nothing, and restored when the power-up is off.
 */
enum {
    APE_SPIKE_PART_RIGHT_HAND = 15,
    APE_SPIKE_PART_LEFT_HAND = 16,
    APE_NET_HANDLE_FIRST = 28,
    APE_NET_HANDLE_LAST = 59,
    APE_NET_HANDLE_COUNT = APE_NET_HANDLE_LAST - APE_NET_HANDLE_FIRST + 1,
    APE_NET_SWOOP_FRAMES = 10
};

typedef struct ApeAffine {
    float r[3][3];
    float t[3];
} ApeAffine;

static struct {
    uint8_t valid;
    ApeAffine right, left;   /* camera space */
} g_spike_hands;

static struct {
    uint32_t base;           /* vertex array the originals came from */
    uint8_t collapsed;
    uint8_t saved[APE_NET_HANDLE_COUNT * 8];
} g_net_handle;

static void ape_read_affine(uint32_t address, ApeAffine* out) {
    ApeMatrix m;
    int row, col;
    ape_read_matrix(address, &m);
    for (row = 0; row < 3; ++row) {
        for (col = 0; col < 3; ++col)
            out->r[row][col] = (float)m.m[row][col] / 4096.0f;
        out->t[row] = (float)m.t[row];
    }
}

static void ape_write_affine(uint32_t address, const ApeAffine* a) {
    int row, col;
    for (row = 0; row < 3; ++row) {
        for (col = 0; col < 3; ++col) {
            psx_mod_write_half(address + (uint32_t)(row * 6 + col * 2),
                               (uint16_t)ape_clamp_s16((int32_t)(a->r[row][col] * 4096.0f)));
        }
        psx_mod_write_word(address + APE_MATRIX_TRANSLATION_OFFSET + (uint32_t)row * 4u,
                           (uint32_t)(int32_t)a->t[row]);
    }
}

static void ape_affine_mul(const ApeAffine* a, const ApeAffine* b, ApeAffine* out) {
    ApeAffine o;
    int i, j, k;
    for (i = 0; i < 3; ++i) {
        for (j = 0; j < 3; ++j) {
            float sum = 0.0f;
            for (k = 0; k < 3; ++k)
                sum += a->r[i][k] * b->r[k][j];
            o.r[i][j] = sum;
        }
        o.t[i] = a->t[i];
        for (k = 0; k < 3; ++k)
            o.t[i] += a->r[i][k] * b->t[k];
    }
    *out = o;
}

/* General inverse: camera-space rotations carry the PS1 aspect scale, so
 * they are not orthonormal. */
static int ape_affine_inverse(const ApeAffine* a, ApeAffine* out) {
    const float (*m)[3] = a->r;
    const float det =
        m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
        m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
        m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    int i, k;
    if (det > -1e-6f && det < 1e-6f)
        return 0;
    out->r[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det;
    out->r[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det;
    out->r[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det;
    out->r[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / det;
    out->r[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det;
    out->r[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det;
    out->r[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det;
    out->r[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det;
    out->r[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
    for (i = 0; i < 3; ++i) {
        out->t[i] = 0.0f;
        for (k = 0; k < 3; ++k)
            out->t[i] -= out->r[i][k] * a->t[k];
    }
    return 1;
}

/* Called for Spike's own transform: keep his hand bones for the net. */
static void ape_snapshot_spike_hands(uint32_t object) {
    const uint32_t parts = psx_mod_read_word(object + 0x14u);
    if (parts == 0u)
        return;
    ape_read_affine(parts + APE_SPIKE_PART_RIGHT_HAND * 0x20u, &g_spike_hands.right);
    ape_read_affine(parts + APE_SPIKE_PART_LEFT_HAND * 0x20u, &g_spike_hands.left);
    g_spike_hands.valid = 1u;
}

/* Hide or restore the handle vertices of the net model. */
static void ape_set_net_handle_hidden(uint32_t net_object, int hidden) {
    const uint32_t base = psx_mod_read_word(net_object + 0x10u);
    const uint32_t first = base + APE_NET_HANDLE_FIRST * 8u;
    uint32_t i;

    if (base == 0u)
        return;
    if (hidden && !g_net_handle.collapsed) {
        /* Only touch the model we expect: v28 is the shaft top (13,56,-4). */
        if ((int16_t)psx_mod_read_half(first) != 13 ||
            (int16_t)psx_mod_read_half(first + 2u) != 56)
            return;
        for (i = 0u; i < APE_NET_HANDLE_COUNT * 8u; ++i)
            g_net_handle.saved[i] = psx_mod_read_byte(first + i);
        /* Collapse onto the bottom of the rim. */
        for (i = 0u; i < APE_NET_HANDLE_COUNT; ++i) {
            psx_mod_write_half(first + i * 8u + 0u, (uint16_t)11);
            psx_mod_write_half(first + i * 8u + 2u, (uint16_t)116);
            psx_mod_write_half(first + i * 8u + 4u, (uint16_t)(int16_t)-7);
        }
        g_net_handle.base = base;
        g_net_handle.collapsed = 1u;
    } else if (!hidden && g_net_handle.collapsed && g_net_handle.base == base) {
        for (i = 0u; i < APE_NET_HANDLE_COUNT * 8u; ++i)
            psx_mod_write_byte(first + i, g_net_handle.saved[i]);
        g_net_handle.collapsed = 0u;
    }
}

/* Re-hang every net part from the right hand onto the left, then (during
 * the swoop) carry the net head from the left hand onto the monkey. */
static void ape_hang_net_in_left_hand(uint32_t net_object, const ApeMatrix* camera) {
    const uint32_t parts = psx_mod_read_word(net_object + 0x14u);
    const uint32_t header = psx_mod_read_word(net_object);
    const int count = header != 0u ? (int)psx_mod_read_half(header + 4u) : 0;
    ApeAffine right_inv, carry, part;
    float swoop[3] = { 0.0f, 0.0f, 0.0f };
    int p, k;

    if (parts == 0u || !g_spike_hands.valid ||
        !ape_affine_inverse(&g_spike_hands.right, &right_inv))
        return;
    ape_affine_mul(&g_spike_hands.left, &right_inv, &carry);

    if (g_grapple.phase == APE_GRAPPLE_SWOOP && g_grapple.monkey != 0u) {
        float m[3], s;
        int row, col;
        ape_monkey_position(g_grapple.monkey, m);
        m[1] -= 60.0f; /* over the monkey, not at its feet */
        s = (float)g_grapple.swoop_frame / (float)APE_NET_SWOOP_FRAMES;
        if (s > 1.0f)
            s = 1.0f;
        s = s * s * (3.0f - 2.0f * s);
        for (row = 0; row < 3; ++row) {
            float v = (float)camera->t[row];
            for (col = 0; col < 3; ++col)
                v += (float)camera->m[row][col] / 4096.0f * m[col];
            swoop[row] = (v - g_spike_hands.left.t[row]) * s;
        }
    }

    for (p = 1; p <= count && p <= APE_NET_MAX_PARTS; ++p) {
        const uint32_t address = parts + (uint32_t)p * 0x20u;
        ape_read_affine(address, &part);
        ape_affine_mul(&carry, &part, &part);
        for (k = 0; k < 3; ++k)
            part.t[k] += swoop[k];
        ape_write_affine(address, &part);
    }
}

/* ---- Grabber ------------------------------------------------------------ */

static void ape_gpu_triangle(const int x[3], const int y[3], uint32_t color) {
    int i;
    psx_mod_write_word(PSX_GPU_GP0_ADDRESS, 0x20000000u | color);
    for (i = 0; i < 3; ++i) {
        psx_mod_write_word(PSX_GPU_GP0_ADDRESS,
                           ((uint32_t)(uint16_t)y[i] << 16u) | (uint16_t)x[i]);
    }
}

/* Three-pronged claw at pos, pointing along fwd; open while searching,
 * closed once it holds a monkey. */
static void ape_draw_grabber(const float pos[3], const float fwd_in[3], int closed) {
    static const uint32_t prong_colors[3] = { 0xA8A8A8u, 0x7C7C7Cu, 0x949494u };
    float fwd[3], up[3] = { 0.0f, -1.0f, 0.0f }, side[3], len;
    const float spread_tip = closed ? 12.0f : 34.0f;
    int k, i;

    len = sqrtf(fwd_in[0] * fwd_in[0] + fwd_in[2] * fwd_in[2]);
    if (len < 1e-3f)
        return;
    fwd[0] = fwd_in[0] / len; fwd[1] = 0.0f; fwd[2] = fwd_in[2] / len;
    side[0] = fwd[2]; side[1] = 0.0f; side[2] = -fwd[0];

    for (k = 0; k < 3; ++k) {
        const float a = (float)k * 2.0943951f;
        float pts[3][3];
        int sx[3], sy[3], ok = 1;
        for (i = 0; i < 3; ++i) {
            const float a2 = a + (i == 0 ? -0.35f : (i == 1 ? 0.35f : 0.0f));
            const float radius = i < 2 ? 12.0f : spread_tip;
            const float reach = i < 2 ? 0.0f : 46.0f;
            const float c = cosf(a2), s = sinf(a2);
            int j;
            for (j = 0; j < 3; ++j)
                pts[i][j] = pos[j] + fwd[j] * reach + (up[j] * c + side[j] * s) * radius;
            if (!ape_project(pts[i], &sx[i], &sy[i]))
                ok = 0;
        }
        if (ok)
            ape_gpu_triangle(sx, sy, prong_colors[k]);
    }
}

static void ape_draw_net_extras(void) {
    float fwd[3];
    if (!g_view.valid || !ape_net_is_held())
        return;
    if (ape_grapple_away()) {
        ape_draw_rope();
        ape_draw_grabber(g_grapple.pos, g_grapple.dir,
                         g_grapple.phase == APE_GRAPPLE_REEL ||
                         g_grapple.phase == APE_GRAPPLE_SWOOP);
    } else {
        float hand[3];
        int k;
        for (k = 0; k < 3; ++k)
            hand[k] = (float)g_view.hand[k];
        fwd[0] = (float)(int16_t)psx_mod_read_half(0x800EC21Cu);
        fwd[1] = 0.0f;
        fwd[2] = (float)(int16_t)psx_mod_read_half(0x800EC220u);
        ape_draw_grabber(hand, fwd, 0);
    }
}

/* ---- Hooks -------------------------------------------------------------- */

/* Once per game frame while the net is drawn: capture view data, hang the
 * net head in the left hand, drag the latched monkey, place hitboxes. */
static void ape_net_transform_entry(struct CPUState* cpu, uint32_t address) {
    ApeMatrix camera, hand;
    float heading[3] = { 0.0f, 0.0f, 0.0f };
    int row, col;
    (void)address;

    if (cpu->gpr[4] == APE_SPIKE_OBJECT) {
        ape_snapshot_spike_hands(cpu->gpr[4]);
        return;
    }
    if (cpu->gpr[31] != APE_NET_TRANSFORM_RETURN)
        return;

    ape_read_matrix(APE_CAMERA_MATRIX, &camera);
    ape_read_matrix(APE_HAND_WORLD_MATRIX, &hand);
    for (row = 0; row < 3; ++row) {
        for (col = 0; col < 3; ++col)
            g_view.rot[row][col] = camera.m[row][col];
        g_view.trans[row] = camera.t[row];
        g_view.hand[row] = hand.t[row];
    }
    g_view.h = (int32_t)(cpu->gte_ctrl[26] & 0xFFFFu);
    g_view.ofx = (int32_t)cpu->gte_ctrl[24];
    g_view.ofy = (int32_t)cpu->gte_ctrl[25];
    g_view.valid = 1u;

    ape_set_net_handle_hidden(cpu->gpr[4], g_net_power.on);
    if (!g_net_power.on)
        return;
    ape_hang_net_in_left_hand(cpu->gpr[4], &camera);

    if ((g_grapple.phase == APE_GRAPPLE_REEL || g_grapple.phase == APE_GRAPPLE_SWOOP) &&
        g_grapple.monkey != 0u) {
        float m[3];
        /* Drag the monkey along with the grabber, on its own ground height. */
        ape_set_monkey_xz(g_grapple.monkey, g_grapple.pos[0], g_grapple.pos[2]);
        ape_monkey_position(g_grapple.monkey, m);
        if (g_grapple.stun_pending) {
            g_grapple.stun_pending = 0u;
            ape_spawn_attack(m, g_grapple.dir, APE_CLUB_HIT_RADIUS,
                             APE_CLUB_HIT_EFFECT, APE_CLUB_HIT_FLAGS,
                             APE_COLLISION_PLAYER_ATTACK);
        }
        if (g_grapple.capture_frames != 0u) {
            ape_spawn_attack(m, heading, APE_NET_HIT_RADIUS, APE_NET_HIT_EFFECT,
                             0u, APE_COLLISION_NET);
            if (--g_grapple.capture_frames == 0u)
                ape_end_grapple();
        }
    }
}

