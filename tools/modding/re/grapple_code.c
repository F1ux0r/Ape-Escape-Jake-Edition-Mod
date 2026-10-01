/* ==== Grapple Net ======================================================== */

/*
 * Powered-up Time Net: a right-stick flick plays the stock net swing as the
 * throw, then the whole net flies out on a rope. The first monkey it touches
 * is stunned and latched, reeled back to Spike, and netted by a type-8
 * (net) sphere so the game's own capture runs.
 *
 * The net is a two-part skinned model. Its handler builds each part's world
 * matrix from the hand (0x800F4910), multiplies it by the camera matrix at
 * 0x800E78C0 into [net+0x14]+0x40 and +0x60, then calls the vertex transform
 * 0x80016A60(net) from 0x80099324. Shifting both parts moves the whole net.
 */
static const uint32_t APE_CAMERA_MATRIX = 0x800E78C0u;
static const uint32_t APE_HAND_WORLD_MATRIX = 0x800F4910u;
static const uint32_t APE_MODEL_TRANSFORM_FUNCTION = 0x80016A60u;
static const uint32_t APE_NET_TRANSFORM_RETURN = 0x8009932Cu;

enum {
    APE_GADGET_TIME_NET = 1u,
    APE_NET_PART_A = 0x40,
    APE_NET_PART_B = 0x60,

    /* Net swing: 0x80 swing, 0x81 catch, 0x83 recover. */
    APE_STATE_NET_FIRST = 0x80u,
    APE_STATE_NET_LAST = 0x83u,

    APE_COLLISION_MONKEY = 4,
    APE_COLLISION_NET = 8,
    /* Values the stock net swing writes. */
    APE_NET_HIT_RADIUS = 0x90,
    APE_NET_HIT_EFFECT = 0x100,
    APE_NET_CAPTURE_FRAMES = 4
};

/* Tuning, in world units and VBlanks. */
static const float APE_GRAPPLE_RANGE = 1800.0f;
static const float APE_GRAPPLE_OUT_SPEED = 75.0f;
static const float APE_GRAPPLE_REEL_SPEED = 42.0f;
static const float APE_GRAPPLE_RETURN_SPEED = 95.0f;
static const float APE_GRAPPLE_LATCH_RADIUS = 110.0f;
static const float APE_GRAPPLE_CAPTURE_DISTANCE = 190.0f;
static const float APE_GRAPPLE_HAND_DISTANCE = 120.0f;
static const uint32_t APE_GRAPPLE_WINDUP_FRAMES = 12u;
static const uint32_t APE_GRAPPLE_TIMEOUT_FRAMES = 360u;

typedef enum ApeGrapplePhase {
    APE_GRAPPLE_IDLE = 0,
    APE_GRAPPLE_WINDUP,
    APE_GRAPPLE_OUT,
    APE_GRAPPLE_REEL,
    APE_GRAPPLE_RETURN
} ApeGrapplePhase;

static ApePowerToggle g_net_power = { APE_GADGET_TIME_NET };

static struct {
    ApeGrapplePhase phase;
    uint32_t frame;
    float pos[3];
    float dir[3];
    float travelled;
    uint32_t monkey;       /* latched monkey actor, 0 if none */
    float monkey_y;        /* its ground height, kept while dragging */
    uint8_t stun_pending;  /* stun it on the next game frame */
    uint8_t capture_frames;
    uint8_t follow_through;
    uint8_t stick_was_active;
} g_grapple;

/* Latest view data seen at the net transform, for aiming and the rope. */
static struct {
    uint8_t valid;
    int32_t rot[3][3];
    int32_t trans[3];
    int32_t hand[3];
    int32_t h;          /* projection plane distance */
    int32_t ofx, ofy;   /* screen offset, 16.16 */
} g_view;

static int ape_net_is_held(void) {
    return ape_gadget_is_held(APE_GADGET_TIME_NET);
}

static int ape_grapple_away(void) {
    return g_grapple.phase == APE_GRAPPLE_OUT ||
           g_grapple.phase == APE_GRAPPLE_REEL ||
           g_grapple.phase == APE_GRAPPLE_RETURN;
}

static void ape_end_grapple(void) {
    g_grapple.phase = APE_GRAPPLE_IDLE;
    g_grapple.monkey = 0u;
    g_grapple.follow_through = 0u;
    g_grapple.capture_frames = 0u;
    g_grapple.stick_was_active = 1u;
}

static float ape_distance(const float a[3], const float b[3]) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static void ape_hand_target(float out[3]) {
    ape_spike_position(out);
    out[1] -= APE_HAND_HEIGHT;
}

static void ape_begin_grapple(float sx, float sy) {
    /* Camera rows are its right/up/forward axes in world space. */
    const float x = sx * (float)g_view.rot[0][0] + sy * (float)g_view.rot[2][0];
    const float z = sx * (float)g_view.rot[0][2] + sy * (float)g_view.rot[2][2];
    const float len = sqrtf(x * x + z * z);
    if (!g_view.valid || len < 1.0f)
        return;
    g_grapple.dir[0] = x / len;
    g_grapple.dir[1] = 0.0f;
    g_grapple.dir[2] = z / len;
    g_grapple.frame = 0u;
    g_grapple.follow_through = 0u;
    g_grapple.phase = APE_GRAPPLE_WINDUP;
}

static void ape_release_grapple(void) {
    int k;
    for (k = 0; k < 3; ++k)
        g_grapple.pos[k] = (float)g_view.hand[k];
    g_grapple.travelled = 0.0f;
    g_grapple.frame = 0u;
    g_grapple.monkey = 0u;
    g_grapple.follow_through = 1u;
    g_grapple.phase = APE_GRAPPLE_OUT;
}

/* Nearest monkey collider within latch range of the net, from the
 * collision list. Returns its actor (the node's owner). */
static uint32_t ape_find_monkey_near(const float pos[3], float* out_y) {
    uint32_t node = psx_mod_read_word(APE_COLLISION_LIST_HEAD);
    uint32_t best = 0u;
    float best_distance = 1e30f;
    int guard;

    for (guard = 0; node != 0u && guard < APE_COLLISION_NODE_COUNT; ++guard) {
        const int16_t type = (int16_t)psx_mod_read_half(node);
        const uint32_t sphere = psx_mod_read_word(node + 4u);
        const uint32_t owner = psx_mod_read_word(node + 8u);
        if (type == APE_COLLISION_MONKEY && sphere != 0u && owner != 0u) {
            float c[3], d;
            c[0] = (float)(int32_t)psx_mod_read_word(sphere + 0u);
            c[1] = (float)(int32_t)psx_mod_read_word(sphere + 4u);
            c[2] = (float)(int32_t)psx_mod_read_word(sphere + 8u);
            d = ape_distance(pos, c) -
                (float)(int16_t)psx_mod_read_half(sphere + 0x20u);
            if (d < APE_GRAPPLE_LATCH_RADIUS && d < best_distance) {
                best_distance = d;
                best = owner;
                *out_y = (float)(int16_t)psx_mod_read_half(owner + 0x1Eu);
            }
        }
        node = psx_mod_read_word(node + 0xCu);
    }
    return best;
}

/* Monkeys share Spike's actor layout: s16 position at +0x1C/+0x1E/+0x20. */
static void ape_monkey_position(uint32_t actor, float out[3]) {
    out[0] = (float)(int16_t)psx_mod_read_half(actor + 0x1Cu);
    out[1] = (float)(int16_t)psx_mod_read_half(actor + 0x1Eu);
    out[2] = (float)(int16_t)psx_mod_read_half(actor + 0x20u);
}

static void ape_move_toward(float pos[3], const float target[3], float step,
                            float* remaining) {
    const float d = ape_distance(pos, target);
    int k;
    *remaining = d;
    if (d <= step || d < 1.0f) {
        for (k = 0; k < 3; ++k)
            pos[k] = target[k];
        *remaining = 0.0f;
        return;
    }
    for (k = 0; k < 3; ++k)
        pos[k] += (target[k] - pos[k]) / d * step;
}

static void ape_update_grapple(void) {
    float sx, sy, hand[3], remaining;
    int stick_active;
    int k;

    ape_read_right_stick(&sx, &sy);
    stick_active = sx * sx + sy * sy >
        (float)(APE_THROW_STICK_THRESHOLD * APE_THROW_STICK_THRESHOLD);

    switch (g_grapple.phase) {
        case APE_GRAPPLE_IDLE:
            if (stick_active && !g_grapple.stick_was_active && ape_net_is_held())
                ape_begin_grapple(sx, sy);
            break;
        case APE_GRAPPLE_WINDUP:
            if (++g_grapple.frame >= APE_GRAPPLE_WINDUP_FRAMES)
                ape_release_grapple();
            break;
        case APE_GRAPPLE_OUT:
            for (k = 0; k < 3; ++k)
                g_grapple.pos[k] += g_grapple.dir[k] * APE_GRAPPLE_OUT_SPEED;
            g_grapple.travelled += APE_GRAPPLE_OUT_SPEED;
            g_grapple.monkey = ape_find_monkey_near(g_grapple.pos, &g_grapple.monkey_y);
            if (g_grapple.monkey != 0u) {
                g_grapple.stun_pending = 1u;
                g_grapple.phase = APE_GRAPPLE_REEL;
            } else if (g_grapple.travelled >= APE_GRAPPLE_RANGE) {
                g_grapple.phase = APE_GRAPPLE_RETURN;
            }
            break;
        case APE_GRAPPLE_REEL:
            ape_hand_target(hand);
            hand[1] = g_grapple.pos[1];
            ape_move_toward(g_grapple.pos, hand, APE_GRAPPLE_REEL_SPEED, &remaining);
            if (remaining <= APE_GRAPPLE_CAPTURE_DISTANCE &&
                g_grapple.capture_frames == 0u)
                g_grapple.capture_frames = APE_NET_CAPTURE_FRAMES;
            break;
        case APE_GRAPPLE_RETURN:
            ape_hand_target(hand);
            ape_move_toward(g_grapple.pos, hand, APE_GRAPPLE_RETURN_SPEED, &remaining);
            if (remaining <= APE_GRAPPLE_HAND_DISTANCE)
                ape_end_grapple();
            break;
    }
    g_grapple.stick_was_active = (uint8_t)stick_active;

    if (ape_grapple_away() && ++g_grapple.frame > APE_GRAPPLE_TIMEOUT_FRAMES)
        ape_end_grapple();
}

/* ---- Rope --------------------------------------------------------------- */

static int ape_project(const float world[3], int* sx, int* sy) {
    int32_t v[3];
    int row, col;
    for (row = 0; row < 3; ++row) {
        int64_t sum = 0;
        for (col = 0; col < 3; ++col)
            sum += (int64_t)g_view.rot[row][col] * (int64_t)world[col];
        v[row] = (int32_t)(sum >> 12) + g_view.trans[row];
    }
    if (v[2] < 64)
        return 0;
    *sx = (int)((g_view.ofx >> 16) + (int64_t)v[0] * g_view.h / v[2]);
    *sy = (int)((g_view.ofy >> 16) + (int64_t)v[1] * g_view.h / v[2]);
    return 1;
}

static void ape_gpu_line(int x0, int y0, int x1, int y1, uint32_t color) {
    psx_mod_write_word(PSX_GPU_GP0_ADDRESS, 0x40000000u | color);
    psx_mod_write_word(PSX_GPU_GP0_ADDRESS,
                       ((uint32_t)(uint16_t)y0 << 16u) | (uint16_t)x0);
    psx_mod_write_word(PSX_GPU_GP0_ADDRESS,
                       ((uint32_t)(uint16_t)y1 << 16u) | (uint16_t)x1);
}

static void ape_draw_rope(void) {
    float hand[3];
    int x0, y0, x1, y1, k;
    if (!g_view.valid || !ape_grapple_away())
        return;
    for (k = 0; k < 3; ++k)
        hand[k] = (float)g_view.hand[k];
    if (!ape_project(hand, &x0, &y0) || !ape_project(g_grapple.pos, &x1, &y1))
        return;
    /* Two strands, a little apart, read as a rope at PS1 resolution. */
    ape_gpu_line(x0, y0, x1, y1, 0x3C6E9Au);   /* tan (BGR) */
    ape_gpu_line(x0 + 1, y0, x1 + 1, y1, 0x284A6Eu);
}

static void ape_grapple_net_vblank(void) {
    uint8_t net_slot;
    ape_update_arm_sfx();
    if (ape_update_power_toggle(&g_net_power, &net_slot) && !g_net_power.on)
        ape_end_grapple();
    if (g_net_power.on) {
        ape_update_grapple();
        ape_draw_rope();
    }
}

/* ---- Hooks -------------------------------------------------------------- */

/* Once per game frame while the net is drawn: capture view data, move the
 * drawn net to the grapple, drag the latched monkey, place hitboxes. */
static void ape_net_transform_entry(struct CPUState* cpu, uint32_t address) {
    ApeMatrix camera, hand;
    uint32_t parts;
    float heading[3] = { 0.0f, 0.0f, 0.0f };
    int row, col, p;
    (void)address;

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

    if (!g_net_power.on || !ape_grapple_away())
        return;

    /* Draw the whole net at the grapple position. */
    parts = psx_mod_read_word(cpu->gpr[4] + 0x14u);
    if (parts != 0u) {
        for (p = 0; p < 2; ++p) {
            const uint32_t part = parts + (p == 0 ? APE_NET_PART_A : APE_NET_PART_B);
            for (row = 0; row < 3; ++row) {
                const uint32_t t = part + APE_MATRIX_TRANSLATION_OFFSET + (uint32_t)row * 4u;
                int32_t shift = 0;
                for (col = 0; col < 3; ++col)
                    shift += camera.m[row][col] *
                             ((int32_t)g_grapple.pos[col] - hand.t[col]);
                psx_mod_write_word(t, psx_mod_read_word(t) + (uint32_t)(shift >> 12));
            }
        }
    }

    if (g_grapple.phase == APE_GRAPPLE_REEL && g_grapple.monkey != 0u) {
        float m[3];
        /* Drag the monkey along with the net, on its own ground height. */
        psx_mod_write_half(g_grapple.monkey + 0x1Cu, (uint16_t)(int16_t)g_grapple.pos[0]);
        psx_mod_write_half(g_grapple.monkey + 0x20u, (uint16_t)(int16_t)g_grapple.pos[2]);
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

/* Stock net swing = the throw. Release at the catch state; while the net is
 * away, new swings become idle (the releasing swing may finish). */
static void ape_net_set_state_entry(struct CPUState* cpu, uint32_t address) {
    const uint32_t state = cpu->gpr[4] & 0xFFu;
    const int net_state = state >= APE_STATE_NET_FIRST && state <= APE_STATE_NET_LAST;
    (void)address;

    if (!g_net_power.on || !ape_net_is_held())
        return;

    if (g_grapple.phase == APE_GRAPPLE_IDLE) {
        if (net_state && state == APE_STATE_NET_FIRST) {
            float sx, sy;
            ape_read_right_stick(&sx, &sy);
            ape_begin_grapple(sx, sy);
        }
    } else if (g_grapple.phase == APE_GRAPPLE_WINDUP) {
        if (state != APE_STATE_NET_FIRST)
            ape_release_grapple();
    } else if (g_grapple.follow_through) {
        if (!net_state)
            g_grapple.follow_through = 0u;
    } else if (net_state) {
        cpu->gpr[4] = APE_STATE_IDLE;
    }
}

PSX_MOD_CONSTRUCTOR(ape_register_grapple_net_plugin) {
    (void)psx_mod_register_vblank_plugin(
        "ape.gadgets.grapple-net", ape_grapple_net_vblank);
    (void)psx_mod_register_function_entry_plugin(
        "ape.gadgets.grapple-net", APE_MODEL_TRANSFORM_FUNCTION,
        ape_net_transform_entry);
    (void)psx_mod_register_function_entry_plugin(
        "ape.gadgets.grapple-net", APE_SET_STATE_FUNCTION,
        ape_net_set_state_entry);
}
