static void ape_hang_net_in_left_hand(uint32_t net_object, const ApeMatrix* camera) {
    const uint32_t parts = psx_mod_read_word(net_object + 0x14u);
    const uint32_t header = psx_mod_read_word(net_object);
    const int count = header != 0u ? (int)psx_mod_read_half(header + 4u) : 0;
    ApeAffine right_inv, carry, hung[APE_NET_MAX_PARTS + 1];
    float grip[3], swoop[3] = { 0.0f, 0.0f, 0.0f };
    const float s = ape_swoop_progress();
    int p, k, n;

    if (parts == 0u || !g_spike_hands.valid || count <= 0 ||
        !ape_affine_inverse(&g_spike_hands.right, &right_inv))
        return;
    n = count < APE_NET_MAX_PARTS ? count : APE_NET_MAX_PARTS;
    {
        /* Turn the grip 180 degrees inside the hand so the net hangs out
         * from Spike's left side instead of across his back. */
        ApeAffine flip = { { { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
                             { 0.0f, 0.0f, -1.0f } }, { 0.0f, 0.0f, 0.0f } };
        ape_affine_mul(&g_spike_hands.left, &flip, &carry);
        ape_affine_mul(&carry, &right_inv, &carry);
    }

    for (p = 1; p <= n; ++p) {
        ape_read_affine(parts + (uint32_t)p * 0x20u, &hung[p]);
        ape_affine_mul(&carry, &hung[p], &hung[p]);
    }

    /* With the handle gone the rim would float where the handle ended: pull
     * the net back along the rim part's own axis so the rim sits in the
     * hand. */
    for (k = 0; k < 3; ++k)
        grip[k] = -hung[1].r[k][1] * APE_NET_RIM_OFFSET;
    for (p = 1; p <= n; ++p)
        for (k = 0; k < 3; ++k)
            hung[p].t[k] += grip[k];

    /* Smaller net head, scaled about the hand; bag parts (2..) pulled in
     * toward the rim so it hangs closer and clips into Spike less. */
    for (p = 1; p <= n; ++p) {
        int i, j;
        for (i = 0; i < 3; ++i) {
            for (j = 0; j < 3; ++j)
                hung[p].r[i][j] *= APE_NET_HEAD_SCALE;
            hung[p].t[i] = g_spike_hands.left.t[i] +
                           (hung[p].t[i] - g_spike_hands.left.t[i]) * APE_NET_HEAD_SCALE;
        }
    }
    for (p = 2; p <= n; ++p)
        for (k = 0; k < 3; ++k)
            hung[p].t[k] = hung[1].t[k] + (hung[p].t[k] - hung[1].t[k]) * APE_NET_BAG_DEPTH;

    /* Swoop: the arm is already reaching (see ape_pose_left_arm); carry the
     * net the rest of the way onto the monkey, mostly late in the motion. */
    if (s > 0.0f) {
        float m[3];
        int row, col;
        ape_swoop_target_world(m);
        for (row = 0; row < 3; ++row) {
            float v = (float)camera->t[row];
            for (col = 0; col < 3; ++col)
                v += (float)camera->m[row][col] / 4096.0f * m[col];
            swoop[row] = (v - hung[1].t[row]) * s * s;
        }
    }

    for (p = 1; p <= n; ++p) {
        for (k = 0; k < 3; ++k)
            hung[p].t[k] += swoop[k];
        ape_write_affine(parts + (uint32_t)p * 0x20u, &hung[p]);
    }
}

