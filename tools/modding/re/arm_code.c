/* Swoop progress 0..1, eased; 0 outside the swoop. */
static float ape_swoop_progress(void) {
    float s;
    if (g_grapple.phase != APE_GRAPPLE_SWOOP || g_grapple.monkey == 0u)
        return 0.0f;
    s = (float)g_grapple.swoop_frame / (float)APE_NET_SWOOP_FRAMES;
    if (s > 1.0f)
        s = 1.0f;
    return s * s * (3.0f - 2.0f * s);
}

/* Where the net should come down: just over the latched monkey. */
static void ape_swoop_target_world(float out[3]) {
    ape_monkey_position(g_grapple.monkey, out);
    out[1] -= 60.0f;
}

static void ape_camera_affine(ApeAffine* out) {
    ape_read_affine(APE_CAMERA_MATRIX, out);
}

static void ape_apply_affine(const ApeAffine* a, const float in[3], float out[3]) {
    int i, k;
    for (i = 0; i < 3; ++i) {
        out[i] = a->t[i];
        for (k = 0; k < 3; ++k)
            out[i] += a->r[i][k] * in[k];
    }
}

/*
 * During the swoop, swing Spike's left arm (shoulder 6, elbow 10, hand 16)
 * about the shoulder so the hand points at the monkey. The turn is built in
 * world space (camera rows carry the PS1 aspect scale) and applied to the
 * camera-space part matrices as  Qv = C * Qw * C^-1  about the shoulder.
 */
static void ape_pose_left_arm(uint32_t parts) {
    static const int arm[3] = { APE_SPIKE_PART_LEFT_SHOULDER,
                                APE_SPIKE_PART_LEFT_ELBOW,
                                APE_SPIKE_PART_LEFT_HAND };
    const float s = ape_swoop_progress();
    ApeAffine cam, cam_inv, part;
    float shoulder_v[3], hand_v[3], shoulder_w[3], hand_w[3], target_w[3];
    float a[3], b[3], axis[3], la, lb, lx, angle, c, sn, qw[3][3], qv[3][3], tmp[3][3];
    int i, j, k, n;

    if (s <= 0.0f)
        return;
    ape_camera_affine(&cam);
    if (!ape_affine_inverse(&cam, &cam_inv))
        return;

    ape_read_affine(parts + APE_SPIKE_PART_LEFT_SHOULDER * 0x20u, &part);
    for (k = 0; k < 3; ++k) shoulder_v[k] = part.t[k];
    ape_read_affine(parts + APE_SPIKE_PART_LEFT_HAND * 0x20u, &part);
    for (k = 0; k < 3; ++k) hand_v[k] = part.t[k];
    ape_apply_affine(&cam_inv, shoulder_v, shoulder_w);
    ape_apply_affine(&cam_inv, hand_v, hand_w);
    ape_swoop_target_world(target_w);

    for (k = 0; k < 3; ++k) {
        a[k] = hand_w[k] - shoulder_w[k];
        b[k] = target_w[k] - shoulder_w[k];
    }
    la = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    lb = sqrtf(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    if (la < 1.0f || lb < 1.0f)
        return;
    for (k = 0; k < 3; ++k) { a[k] /= la; b[k] /= lb; }
    axis[0] = a[1] * b[2] - a[2] * b[1];
    axis[1] = a[2] * b[0] - a[0] * b[2];
    axis[2] = a[0] * b[1] - a[1] * b[0];
    lx = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (lx < 1e-4f)
        return;
    for (k = 0; k < 3; ++k) axis[k] /= lx;
    angle = acosf(fmaxf(-1.0f, fminf(1.0f, a[0] * b[0] + a[1] * b[1] + a[2] * b[2])));
    if (angle > APE_ARM_MAX_SWING)
        angle = APE_ARM_MAX_SWING;
    angle *= s;

    /* Rodrigues. */
    c = cosf(angle); sn = sinf(angle);
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j)
            qw[i][j] = (i == j ? c : 0.0f) + (1.0f - c) * axis[i] * axis[j];
    qw[0][1] -= sn * axis[2]; qw[0][2] += sn * axis[1];
    qw[1][0] += sn * axis[2]; qw[1][2] -= sn * axis[0];
    qw[2][0] -= sn * axis[1]; qw[2][1] += sn * axis[0];

    /* Qv = C.r * Qw * C^-1.r */
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j) {
            tmp[i][j] = 0.0f;
            for (k = 0; k < 3; ++k) tmp[i][j] += qw[i][k] * cam_inv.r[k][j];
        }
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j) {
            qv[i][j] = 0.0f;
            for (k = 0; k < 3; ++k) qv[i][j] += cam.r[i][k] * tmp[k][j];
        }

    for (n = 0; n < 3; ++n) {
        const uint32_t address = parts + (uint32_t)arm[n] * 0x20u;
        ApeAffine out;
        float d[3];
        ape_read_affine(address, &part);
        for (k = 0; k < 3; ++k) d[k] = part.t[k] - shoulder_v[k];
        for (i = 0; i < 3; ++i) {
            out.t[i] = shoulder_v[i];
            for (k = 0; k < 3; ++k) out.t[i] += qv[i][k] * d[k];
            for (j = 0; j < 3; ++j) {
                out.r[i][j] = 0.0f;
                for (k = 0; k < 3; ++k) out.r[i][j] += qv[i][k] * part.r[k][j];
            }
        }
        ape_write_affine(address, &out);
    }
}

/* Called for Spike's own transform: pose the left arm during the swoop,
 * then keep his hand bones for the net. */
static void ape_snapshot_spike_hands(uint32_t object) {
    const uint32_t parts = psx_mod_read_word(object + 0x14u);
    if (parts == 0u)
        return;
    if (g_net_power.on)
        ape_pose_left_arm(parts);
    ape_read_affine(parts + APE_SPIKE_PART_RIGHT_HAND * 0x20u, &g_spike_hands.right);
    ape_read_affine(parts + APE_SPIKE_PART_LEFT_HAND * 0x20u, &g_spike_hands.left);
    g_spike_hands.valid = 1u;
}
