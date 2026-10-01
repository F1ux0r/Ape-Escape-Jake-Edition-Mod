
/* ---- Throw ------------------------------------------------------------- */

/* Spike's world position (s16 x, y, z; y grows downward), found by diffing
 * RAM while walking. */
static const uint32_t APE_SPIKE_POSITION_ADDRESS = 0x800EC1FCu;
static const uint32_t APE_PAD_ID_ADDRESS = 0x800B87A0u;
static const uint32_t APE_RIGHT_STICK_X_ADDRESS = 0x800B87A4u;
static const uint32_t APE_RIGHT_STICK_Y_ADDRESS = 0x800B87A5u;

/* set_state(a0 = new Spike state), called by the action-script interpreter.
 * The club swing runs states 0x85..0x8D; 0 is idle. */
static const uint32_t APE_SET_STATE_FUNCTION = 0x80064208u;
/* Club model draw: a0 = hand matrix in camera space, a1 = hand in world. */
static const uint32_t APE_CLUB_DRAW_FUNCTION = 0x8009ABF4u;

enum {
    APE_PAD_ID_ANALOG = 0x73u,
    APE_THROW_STICK_THRESHOLD = 72,

    APE_STATE_IDLE = 0x00u,
    APE_STATE_CLUB_FIRST = 0x83u,
    APE_STATE_CLUB_LAST = 0x8Fu,

    /* PS1 MATRIX: s16 m[3][3], 2 bytes pad, s32 t[3]. */
    APE_MATRIX_TRANSLATION_OFFSET = 20
};

/* Flight tuning, in world units and VBlanks (60 per second). */
static const float APE_THROW_RANGE = 1100.0f;
static const float APE_THROW_OUT_FRAMES = 26.0f;
static const float APE_THROW_CURVE = 0.22f;
static const float APE_RETURN_ACCEL = 5.0f;
static const float APE_RETURN_MAX_SPEED = 110.0f;
static const float APE_CATCH_DISTANCE = 110.0f;
static const float APE_HAND_HEIGHT = 230.0f;
static const float APE_SPIN_PER_FRAME = 0.62f; /* radians */
static const uint32_t APE_THROW_TIMEOUT_FRAMES = 300u;

typedef struct ApeMatrix {
    int32_t m[3][3];
    int32_t t[3];
} ApeMatrix;

typedef enum ApeThrowPhase {
    APE_THROW_IDLE = 0,
    APE_THROW_OUT,
    APE_THROW_RETURN
} ApeThrowPhase;

static struct {
    ApeThrowPhase phase;
    uint32_t frame;
    float pos[3];
    float dir[3];  /* unit, horizontal */
    float side[3]; /* unit, horizontal, perpendicular to dir */
    float speed;
    float spin;
    int32_t base_rot[3][3]; /* hand's world rotation at release */
} g_throw;

/* Latest hand matrices seen at the club draw, used to aim and release. */
static int32_t g_camera_rot[3][3];
static int32_t g_hand_world_rot[3][3];
static int32_t g_hand_world_pos[3];
static uint8_t g_have_hand;
static uint8_t g_stick_was_active;

static void ape_read_matrix(uint32_t address, ApeMatrix* out) {
    int row, col;
    for (row = 0; row < 3; ++row) {
        for (col = 0; col < 3; ++col) {
            out->m[row][col] = (int16_t)psx_mod_read_half(
                address + (uint32_t)(row * 6 + col * 2));
        }
        out->t[row] = (int32_t)psx_mod_read_word(
            address + APE_MATRIX_TRANSLATION_OFFSET + (uint32_t)row * 4u);
    }
}

static void ape_spike_position(float out[3]) {
    out[0] = (float)(int16_t)psx_mod_read_half(APE_SPIKE_POSITION_ADDRESS);
    out[1] = (float)(int16_t)psx_mod_read_half(APE_SPIKE_POSITION_ADDRESS + 2u);
    out[2] = (float)(int16_t)psx_mod_read_half(APE_SPIKE_POSITION_ADDRESS + 4u);
}

static int ape_throw_active(void) {
    return g_throw.phase != APE_THROW_IDLE;
}

static void ape_end_throw(void) {
    g_throw.phase = APE_THROW_IDLE;
    g_stick_was_active = 1u; /* require a fresh flick for the next throw */
}

/*
 * Stick direction to a horizontal world direction, relative to the camera
 * (up = away from the camera). Camera rows are its right/up/forward axes in
 * world space.
 */
static int ape_throw_direction(float sx, float sy, float out[3]) {
    const float right_x = (float)g_camera_rot[0][0];
    const float right_z = (float)g_camera_rot[0][2];
    const float fwd_x = (float)g_camera_rot[2][0];
    const float fwd_z = (float)g_camera_rot[2][2];
    const float x = sx * right_x + sy * fwd_x;
    const float z = sx * right_z + sy * fwd_z;
    const float len = sqrtf(x * x + z * z);
    if (len < 1.0f)
        return 0;
    out[0] = x / len;
    out[1] = 0.0f;
    out[2] = z / len;
    return 1;
}

static void ape_start_throw(float sx, float sy) {
    int row, col;
    if (!g_have_hand || !ape_throw_direction(sx, sy, g_throw.dir))
        return;

    g_throw.side[0] = g_throw.dir[2];
    g_throw.side[1] = 0.0f;
    g_throw.side[2] = -g_throw.dir[0];
    for (row = 0; row < 3; ++row) {
        g_throw.pos[row] = (float)g_hand_world_pos[row];
        for (col = 0; col < 3; ++col)
            g_throw.base_rot[row][col] = g_hand_world_rot[row][col];
    }
    /* Linear deceleration to zero over the out phase covers the range. */
    g_throw.speed = 2.0f * APE_THROW_RANGE / APE_THROW_OUT_FRAMES;
    g_throw.spin = 0.0f;
    g_throw.frame = 0u;
    g_throw.phase = APE_THROW_OUT;
}

static void ape_update_throw(void) {
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

    ++g_throw.frame;
    g_throw.spin += APE_SPIN_PER_FRAME;

    if (g_throw.phase == APE_THROW_OUT) {
        const float t = (float)g_throw.frame / APE_THROW_OUT_FRAMES;
        for (k = 0; k < 3; ++k) {
            g_throw.pos[k] += g_throw.dir[k] * g_throw.speed +
                              g_throw.side[k] * g_throw.speed *
                                  APE_THROW_CURVE * t;
        }
        g_throw.speed -= 2.0f * APE_THROW_RANGE /
                         (APE_THROW_OUT_FRAMES * APE_THROW_OUT_FRAMES);
        if (g_throw.frame >= (uint32_t)APE_THROW_OUT_FRAMES) {
            g_throw.phase = APE_THROW_RETURN;
            g_throw.speed = 0.0f;
        }
    } else {
        float target[3], delta[3], dist;
        ape_spike_position(target);
        target[1] -= APE_HAND_HEIGHT;
        for (k = 0; k < 3; ++k)
            delta[k] = target[k] - g_throw.pos[k];
        dist = sqrtf(delta[0] * delta[0] + delta[1] * delta[1] +
                     delta[2] * delta[2]);
        g_throw.speed += APE_RETURN_ACCEL;
        if (g_throw.speed > APE_RETURN_MAX_SPEED)
            g_throw.speed = APE_RETURN_MAX_SPEED;
        if (dist <= APE_CATCH_DISTANCE || dist <= g_throw.speed ||
            g_throw.frame > APE_THROW_TIMEOUT_FRAMES) {
            ape_end_throw();
            return;
        }
        for (k = 0; k < 3; ++k)
            g_throw.pos[k] += delta[k] / dist * g_throw.speed;
    }
}

/* World rotation of the flying club: spin about the vertical axis applied on
 * top of the hand's rotation at release. */
static void ape_flying_rotation(int32_t out[3][3]) {
    const float c = cosf(g_throw.spin), s = sinf(g_throw.spin);
    const float spin[3][3] = {
        { c, 0.0f, s }, { 0.0f, 1.0f, 0.0f }, { -s, 0.0f, c }
    };
    int row, col, k;
    for (row = 0; row < 3; ++row) {
        for (col = 0; col < 3; ++col) {
            float sum = 0.0f;
            for (k = 0; k < 3; ++k)
                sum += spin[row][k] * (float)g_throw.base_rot[k][col];
            out[row][col] = (int32_t)sum;
        }
    }
}

static int16_t ape_clamp_s16(int32_t value) {
    if (value > 32767)
        return 32767;
    if (value < -32768)
        return -32768;
    return (int16_t)value;
}

static void ape_club_draw_entry(struct CPUState* cpu, uint32_t address) {
    const uint32_t view_address = cpu->gpr[4];
    const uint32_t world_address = cpu->gpr[5];
    ApeMatrix view, world;
    int32_t rot[3][3];
    int row, col, k;
    (void)address;

    if (view_address == 0u || world_address == 0u)
        return;

    ape_read_matrix(view_address, &view);
    ape_read_matrix(world_address, &world);

    /* R_cam = V.R * W.R^T: both matrices are the same hand. */
    for (row = 0; row < 3; ++row) {
        for (col = 0; col < 3; ++col) {
            int32_t sum = 0;
            for (k = 0; k < 3; ++k)
                sum += view.m[row][k] * world.m[col][k];
            g_camera_rot[row][col] = sum >> 12;
            g_hand_world_rot[row][col] = world.m[row][col];
        }
        g_hand_world_pos[row] = world.t[row];
    }
    g_have_hand = 1u;

    if (!g_boomerang_mode || !ape_throw_active())
        return;

    ape_flying_rotation(rot);
    for (row = 0; row < 3; ++row) {
        int32_t shift = 0;
        for (col = 0; col < 3; ++col) {
            int32_t sum = 0;
            for (k = 0; k < 3; ++k)
                sum += g_camera_rot[row][k] * rot[k][col];
            psx_mod_write_half(view_address + (uint32_t)(row * 6 + col * 2),
                               (uint16_t)ape_clamp_s16(sum >> 12));
            shift += g_camera_rot[row][col] *
                     ((int32_t)g_throw.pos[col] - world.t[col]);
        }
        psx_mod_write_word(view_address + APE_MATRIX_TRANSLATION_OFFSET +
                               (uint32_t)row * 4u,
                           (uint32_t)(view.t[row] + (shift >> 12)));
    }
}

/* In boomerang mode the right stick throws, so stock swing states become
 * idle. */
static void ape_set_state_entry(struct CPUState* cpu, uint32_t address) {
    const uint32_t state = cpu->gpr[4] & 0xFFu;
    (void)address;
    if (g_boomerang_mode && ape_club_is_held() &&
        state >= APE_STATE_CLUB_FIRST && state <= APE_STATE_CLUB_LAST) {
        cpu->gpr[4] = APE_STATE_IDLE;
    }
}

PSX_MOD_CONSTRUCTOR(ape_register_boomerang_club_plugin) {
    (void)psx_mod_register_vblank_plugin(
        "ape.gadgets.boomerang-club", ape_boomerang_club_vblank);
    (void)psx_mod_register_function_entry_plugin(
        "ape.gadgets.boomerang-club", APE_CLUB_DRAW_FUNCTION,
        ape_club_draw_entry);
    (void)psx_mod_register_function_entry_plugin(
        "ape.gadgets.boomerang-club", APE_SET_STATE_FUNCTION,
        ape_set_state_entry);
}
