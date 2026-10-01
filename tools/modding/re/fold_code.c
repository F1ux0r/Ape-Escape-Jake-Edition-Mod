
/* ---- AE2 carry pose and folding blades ---------------------------------- */

/*
 * AE2 carries the flyer by its grip, pointing down, blades folded away; using
 * it swings it up and the blades grow out to full length. "Using" is Spike's
 * flyer states 0x80..0x84 (spin-up, lift, flying). g_flyer_unfold eases
 * between 0 (carried) and 1 (in use) and drives both the blade length and
 * how far the flyer is turned toward pointing down.
 */
enum {
    APE_STATE_FLYER_FIRST = 0x80u,
    APE_STATE_FLYER_LAST = 0x84u
};
static const float APE_FLYER_UNFOLD_RATE = 0.16f;   /* per VBlank */
static const float APE_FLYER_FOLDED_TIP = 44.0f;
static const float APE_FLYER_FOLDED_ROOT = 16.0f;
static const float APE_FLYER_FOLDED_HALF = 4.0f;
/* Carried: slide the flyer along its axis so the grip sits in the fist. */
static const float APE_FLYER_CARRY_SHIFT = 22.0f;
static float g_flyer_unfold = 1.0f;

static void ape_update_flyer_unfold(void) {
    const uint32_t state = psx_mod_read_half(APE_SPIKE_OBJECT + 0x5Eu);
    const float target = (state >= APE_STATE_FLYER_FIRST && state <= APE_STATE_FLYER_LAST) ? 1.0f : 0.0f;
    if (g_flyer_unfold < target) {
        g_flyer_unfold += APE_FLYER_UNFOLD_RATE;
        if (g_flyer_unfold > target)
            g_flyer_unfold = target;
    } else if (g_flyer_unfold > target) {
        g_flyer_unfold -= APE_FLYER_UNFOLD_RATE;
        if (g_flyer_unfold < target)
            g_flyer_unfold = target;
    }
}

static float ape_lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/* Rewrite both blades for the current unfold amount. */
static void ape_shape_blades_folded(float u) {
    static const int b1_root[3] = { 0, 2, 16 }, b1_tip[3] = { 1, 3, 17 };
    static const int b2_root[3] = { 13, 15, 19 }, b2_tip[3] = { 12, 14, 18 };
    const float e = u * u * (3.0f - 2.0f * u);
    const float root = ape_lerp(APE_FLYER_FOLDED_ROOT, APE_FLYER_BLADE_ROOT, e);
    const float tip = ape_lerp(APE_FLYER_FOLDED_TIP, APE_FLYER_BLADE_TIP, e);
    const float root_half = ape_lerp(APE_FLYER_FOLDED_HALF, APE_FLYER_BLADE_ROOT_HALF, e);
    const float tip_half = ape_lerp(APE_FLYER_FOLDED_HALF, APE_FLYER_BLADE_TIP_HALF, e);
    const float edges[3] = { 1.0f, -1.0f, 0.0f };
    const float c = APE_FLYER_BLADE_CENTRE_Z;
    const uint32_t base = g_flyer_model.prop_base;
    int side, i;
    if (!g_flyer_model.applied || base == 0u)
        return;
    for (side = 0; side < 2; ++side) {
        const int* r = side == 0 ? b1_root : b2_root;
        const int* t = side == 0 ? b1_tip : b2_tip;
        const float sign = side == 0 ? 1.0f : -1.0f;
        for (i = 0; i < 3; ++i) {
            ape_set_vertex(base, r[i], (float)g_flyer_model.prop[r[i]][0], sign * root,
                           c + edges[i] * root_half);
            ape_set_vertex(base, t[i], (float)g_flyer_model.prop[t[i]][0], sign * tip,
                           c + edges[i] * tip_half);
        }
    }
}

/*
 * Turn the flyer's model matrix (camera space, 0x800E9E40) so its spin axis
 * (model +x, blade side) points straight down, by (1 - unfold). Built in
 * world space through the camera (whose rows carry the PS1 aspect scale).
 */
static void ape_pose_flyer_matrix(void) {
    const float carry = 1.0f - g_flyer_unfold;
    ApeAffine m, cam, cam_inv;
    float axis[3], target[3] = { 0.0f, 1.0f, 0.0f }, rot_axis[3], wr[3][3];
    float angle, c, s, q[3][3], tmp[3][3], len;
    int i, j, k;

    if (carry <= 0.0f)
        return;
    ape_read_affine(APE_FLYER_MODEL_MATRIX, &m);
    ape_camera_affine(&cam);
    if (!ape_affine_inverse(&cam, &cam_inv))
        return;
    /* model rotation in world space: W = C^-1 * M */
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j) {
            wr[i][j] = 0.0f;
            for (k = 0; k < 3; ++k)
                wr[i][j] += cam_inv.r[i][k] * m.r[k][j];
        }
    for (i = 0; i < 3; ++i)
        axis[i] = wr[i][0];
    ape_normalize3(axis);
    ape_cross3(axis, target, rot_axis);
    len = sqrtf(rot_axis[0] * rot_axis[0] + rot_axis[1] * rot_axis[1] + rot_axis[2] * rot_axis[2]);
    angle = acosf(fmaxf(-1.0f, fminf(1.0f, axis[0] * target[0] + axis[1] * target[1] + axis[2] * target[2])));
    if (len < 1e-4f)
        rot_axis[0] = 1.0f, rot_axis[1] = 0.0f, rot_axis[2] = 0.0f;
    else
        for (i = 0; i < 3; ++i)
            rot_axis[i] /= len;
    angle *= carry * carry * (3.0f - 2.0f * carry);

    c = cosf(angle); s = sinf(angle);
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j)
            q[i][j] = (i == j ? c : 0.0f) + (1.0f - c) * rot_axis[i] * rot_axis[j];
    q[0][1] -= s * rot_axis[2]; q[0][2] += s * rot_axis[1];
    q[1][0] += s * rot_axis[2]; q[1][2] -= s * rot_axis[0];
    q[2][0] -= s * rot_axis[1]; q[2][1] += s * rot_axis[0];

    /* M' = C * (Q * W), translation kept, then slid along the new axis so
     * the grip is in the fist while carried. */
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j) {
            tmp[i][j] = 0.0f;
            for (k = 0; k < 3; ++k)
                tmp[i][j] += q[i][k] * wr[k][j];
        }
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j) {
            m.r[i][j] = 0.0f;
            for (k = 0; k < 3; ++k)
                m.r[i][j] += cam.r[i][k] * tmp[k][j];
        }
    for (i = 0; i < 3; ++i)
        m.t[i] += m.r[i][0] * APE_FLYER_CARRY_SHIFT * carry;
    ape_write_affine(APE_FLYER_MODEL_MATRIX, &m);
}
