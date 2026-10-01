p = "src/mods/ape_boomerang_club_plugin.c"
s = open(p).read()

def sub(old, new):
    global s
    assert s.count(old) == 1, old[:60]
    s = s.replace(old, new)

# ---- Circular glow -------------------------------------------------------
sub("""    /* Glow box around a 32x32 HUD icon, centred on the slot position. */
    APE_GLOW_HALF = 18,
    APE_GLOW_BORDER = 2,
""", """    /* Glow disc and ring around the round HUD icon, centred on the slot
     * position. The icon's dark disc has a radius of about 13 pixels. */
    APE_GLOW_DISC_RADIUS = 13,
    APE_GLOW_RING_OUTER = 16,
""")

start = s.index("static void ape_draw_club_glow(uint8_t slot) {")
end = s.index("static void ape_end_throw(void);")
s = s[:start] + """/* One horizontal span of a disc/annulus row, clipped to [inner, outer). */
static void ape_glow_row(int cx, int y, float dy, float inner, float outer,
                         uint32_t color) {
    const float outer_sq = outer * outer - dy * dy;
    const float inner_sq = inner * inner - dy * dy;
    int outer_half, inner_half;

    if (outer_sq <= 0.0f)
        return;
    outer_half = (int)(sqrtf(outer_sq) + 0.5f);
    inner_half = inner_sq > 0.0f ? (int)(sqrtf(inner_sq) + 0.5f) : 0;
    if (inner_half <= 0) {
        ape_gpu_rect(cx - outer_half, y, outer_half * 2, 1, color, 1);
        return;
    }
    ape_gpu_rect(cx - outer_half, y, outer_half - inner_half, 1, color, 1);
    ape_gpu_rect(cx + inner_half, y, outer_half - inner_half, 1, color, 1);
}

static void ape_draw_club_glow(uint8_t slot) {
    const uint32_t entry = APE_HUD_SLOT_POSITION_TABLE + (uint32_t)slot * 4u;
    const int cx = (int16_t)psx_mod_read_half(entry);
    const int cy = (int16_t)psx_mod_read_half(entry + 2u);
    const uint32_t pulse = ape_glow_pulse();
    /* Cyan, so it reads apart from the stock orange "equipped" ring. */
    const uint32_t fill_level = 20u + (pulse * 44u) / 255u;
    const uint32_t edge_level = 90u + (pulse * 165u) / 255u;
    const uint32_t fill = (fill_level / 3u) | (fill_level << 8u) |
                          (fill_level << 16u);
    const uint32_t edge = (edge_level / 3u) | (edge_level << 8u) |
                          (edge_level << 16u);
    uint32_t saved_texpage;
    int row;

    /*
     * Same GPU-state contract as the quick-select row: drawing offset and
     * area are inherited from the game (this runs before its VBlank flip),
     * the texpage is ours and restored from GPUSTAT afterwards. The GPU has
     * no circle primitive, so the disc and ring are drawn as 1-pixel rows.
     */
    saved_texpage = psx_mod_read_word(PSX_GPU_GPUSTAT_ADDRESS) & 0x7FFu;
    psx_mod_write_word(PSX_GPU_GP0_ADDRESS, 0xE1000000u | APE_TEXPAGE_ADDITIVE);

    for (row = -APE_GLOW_RING_OUTER; row < APE_GLOW_RING_OUTER; ++row) {
        const float dy = (float)row + 0.5f;
        ape_glow_row(cx, cy + row, dy, 0.0f, (float)APE_GLOW_DISC_RADIUS, fill);
        ape_glow_row(cx, cy + row, dy, (float)APE_GLOW_DISC_RADIUS,
                     (float)APE_GLOW_RING_OUTER, edge);
    }

    psx_mod_write_word(PSX_GPU_GP0_ADDRESS, 0xE1000000u | saved_texpage);
}

""" + s[end:]

# ---- Throw animation -----------------------------------------------------
sub(""" * While armed, a right-stick flick throws the club instead of swinging it:
 * it spins out along the stick direction (camera-relative), curves, then
 * homes back to Spike's hand.""", """ * While armed, a right-stick flick throws the club: Spike's stock swing plays
 * as the throwing motion and the club is released at the end of its forward
 * arc, then spins out along the stick direction (camera-relative), curves,
 * and homes back to Spike's hand.""")

sub("""typedef enum ApeThrowPhase {
    APE_THROW_IDLE = 0,
    APE_THROW_OUT,
    APE_THROW_RETURN
} ApeThrowPhase;""", """typedef enum ApeThrowPhase {
    APE_THROW_IDLE = 0,
    APE_THROW_WINDUP, /* swing animation playing, club still in hand */
    APE_THROW_OUT,
    APE_THROW_RETURN
} ApeThrowPhase;""")

sub("""    float speed;
    float spin;
} g_throw;""", """    float speed;
    float spin;
    uint8_t swing_state;    /* first swing state seen during the windup */
    uint8_t follow_through; /* let the releasing swing finish */
} g_throw;""")

sub("""static const uint32_t APE_THROW_TIMEOUT_FRAMES = 300u;""", """static const uint32_t APE_THROW_TIMEOUT_FRAMES = 300u;
/* Release anyway if the swing never advances (e.g. it was interrupted). */
static const uint32_t APE_WINDUP_TIMEOUT_FRAMES = 30u;""")

sub("""static int ape_throw_active(void) {
    return g_throw.phase != APE_THROW_IDLE;
}

static void ape_end_throw(void) {
    g_throw.phase = APE_THROW_IDLE;
    g_stick_was_active = 1u; /* require a fresh flick for the next throw */
}""", """static int ape_throw_active(void) {
    return g_throw.phase != APE_THROW_IDLE;
}

/* Club out of the hand: drawn in flight and carrying a hitbox. */
static int ape_club_in_flight(void) {
    return g_throw.phase == APE_THROW_OUT || g_throw.phase == APE_THROW_RETURN;
}

static void ape_end_throw(void) {
    g_throw.phase = APE_THROW_IDLE;
    g_throw.follow_through = 0u;
    g_stick_was_active = 1u; /* require a fresh flick for the next throw */
}""")

sub("""static void ape_start_throw(float sx, float sy) {
    int k;
    if (!g_have_hand || !ape_throw_direction(sx, sy, g_throw.dir))
        return;

    g_throw.side[0] = g_throw.dir[2];
    g_throw.side[1] = 0.0f;
    g_throw.side[2] = -g_throw.dir[0];
    for (k = 0; k < 3; ++k)
        g_throw.pos[k] = (float)g_hand_world_pos[k];
    /* Linear deceleration to zero over the out phase covers the range. */
    g_throw.speed = 2.0f * APE_THROW_RANGE / APE_THROW_OUT_FRAMES;
    g_throw.spin = 0.0f;
    g_throw.frame = 0u;
    g_throw.phase = APE_THROW_OUT;
}""", """/* Aim from the stick and let the swing animation start. */
static void ape_begin_windup(float sx, float sy) {
    if (!g_have_hand || !ape_throw_direction(sx, sy, g_throw.dir))
        return;
    g_throw.side[0] = g_throw.dir[2];
    g_throw.side[1] = 0.0f;
    g_throw.side[2] = -g_throw.dir[0];
    g_throw.swing_state = 0u;
    g_throw.follow_through = 0u;
    g_throw.frame = 0u;
    g_throw.phase = APE_THROW_WINDUP;
}

/* Let go of the club from wherever the hand is now. */
static void ape_release_throw(void) {
    int k;
    for (k = 0; k < 3; ++k)
        g_throw.pos[k] = (float)g_hand_world_pos[k];
    /* Linear deceleration to zero over the out phase covers the range. */
    g_throw.speed = 2.0f * APE_THROW_RANGE / APE_THROW_OUT_FRAMES;
    g_throw.spin = 0.0f;
    g_throw.frame = 0u;
    g_throw.follow_through = 1u;
    g_throw.phase = APE_THROW_OUT;
}

static void ape_read_right_stick(float* sx, float* sy) {
    const int analog =
        psx_mod_read_byte(APE_PAD_ID_ADDRESS) == APE_PAD_ID_ANALOG;
    *sx = analog
        ? (float)((int)psx_mod_read_byte(APE_RIGHT_STICK_X_ADDRESS) - 0x80)
        : 0.0f;
    *sy = analog
        ? (float)(0x80 - (int)psx_mod_read_byte(APE_RIGHT_STICK_Y_ADDRESS))
        : 0.0f;
}""")

sub("""static void ape_update_throw(void) {
    const int analog =
        psx_mod_read_byte(APE_PAD_ID_ADDRESS) == APE_PAD_ID_ANALOG;
    const float sx = analog
        ? (float)((int)psx_mod_read_byte(APE_RIGHT_STICK_X_ADDRESS) - 0x80)
        : 0.0f;
    const float sy = analog
        ? (float)(0x80 - (int)psx_mod_read_byte(APE_RIGHT_STICK_Y_ADDRESS))
        : 0.0f;
    const int stick_active =
        sx * sx + sy * sy >
        (float)(APE_THROW_STICK_THRESHOLD * APE_THROW_STICK_THRESHOLD);
    int k;

    if (!ape_throw_active()) {
        if (stick_active && !g_stick_was_active && ape_club_is_held())
            ape_start_throw(sx, sy);
        g_stick_was_active = (uint8_t)stick_active;
        return;
    }
    g_stick_was_active = (uint8_t)stick_active;
""", """static void ape_update_throw(void) {
    float sx, sy;
    int stick_active;
    int k;

    ape_read_right_stick(&sx, &sy);
    stick_active =
        sx * sx + sy * sy >
        (float)(APE_THROW_STICK_THRESHOLD * APE_THROW_STICK_THRESHOLD);

    if (!ape_throw_active()) {
        if (stick_active && !g_stick_was_active && ape_club_is_held())
            ape_begin_windup(sx, sy);
        g_stick_was_active = (uint8_t)stick_active;
        return;
    }
    g_stick_was_active = (uint8_t)stick_active;

    if (g_throw.phase == APE_THROW_WINDUP) {
        if (++g_throw.frame > APE_WINDUP_TIMEOUT_FRAMES)
            ape_release_throw();
        return;
    }
""")

sub("""    if (!g_boomerang_mode || !ape_throw_active())
        return;

    ape_flying_rotation(rot);""", """    if (!g_boomerang_mode || !ape_club_in_flight())
        return;

    ape_flying_rotation(rot);""")

sub("""/* In boomerang mode the right stick throws, so stock swing states become
 * idle. */
static void ape_set_state_entry(struct CPUState* cpu, uint32_t address) {
    const uint32_t state = cpu->gpr[4] & 0xFFu;
    (void)address;
    if (g_boomerang_mode && ape_club_is_held() &&
        state >= APE_STATE_CLUB_FIRST && state <= APE_STATE_CLUB_LAST) {
        cpu->gpr[4] = APE_STATE_IDLE;
    }
}""", """/*
 * The stock swing is the throwing motion. A swing that starts while the club
 * is in hand becomes the windup; the club is released when the swing leaves
 * its first (forward-arc) state. The rest of that swing plays as the
 * follow-through, and any new swing while the club is away becomes idle.
 */
static void ape_set_state_entry(struct CPUState* cpu, uint32_t address) {
    const uint32_t state = cpu->gpr[4] & 0xFFu;
    const int club_state =
        state >= APE_STATE_CLUB_FIRST && state <= APE_STATE_CLUB_LAST;
    (void)address;

    if (!g_boomerang_mode || !ape_club_is_held())
        return;

    switch (g_throw.phase) {
        case APE_THROW_IDLE:
            if (club_state) {
                /* The game saw the flick before our VBlank did. */
                float sx, sy;
                ape_read_right_stick(&sx, &sy);
                ape_begin_windup(sx, sy);
                if (g_throw.phase == APE_THROW_WINDUP)
                    g_throw.swing_state = (uint8_t)state;
                else
                    cpu->gpr[4] = APE_STATE_IDLE;
            }
            break;
        case APE_THROW_WINDUP:
            if (club_state && g_throw.swing_state == 0u)
                g_throw.swing_state = (uint8_t)state;
            else if (g_throw.swing_state != 0u && state != g_throw.swing_state)
                ape_release_throw();
            break;
        default:
            if (g_throw.follow_through) {
                if (!club_state)
                    g_throw.follow_through = 0u;
            } else if (club_state) {
                cpu->gpr[4] = APE_STATE_IDLE;
            }
            break;
    }
}""")

sub("""    if (g_boomerang_mode && ape_throw_active())
        ape_spawn_flying_hitbox();""", """    if (g_boomerang_mode && ape_club_in_flight())
        ape_spawn_flying_hitbox();""")

open(p, "w", newline="").write(s)
print("applied")
