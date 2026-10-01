
/* ---- Hitboxes ---------------------------------------------------------- */

/*
 * The stock swing makes one-shot attack spheres each frame (0x8007C468): a
 * 0x38-byte entry from a 128-slot pool, registered as a type-6 (player
 * attack) node on the collision list. The collision pass at 0x80072800
 * consumes and frees them the next frame. The flying club gets the same
 * sphere, built with the same field values the swing writes.
 */
static const uint32_t APE_STUN_CLUB_HANDLER = 0x8009A800u;
static const uint32_t APE_HITBOX_POOL = 0x800BD1A0u;
static const uint32_t APE_HITBOX_FLAGS = 0x800BEDA0u; /* s16 per slot, <0 free */
static const uint32_t APE_COLLISION_NODE_POOL = 0x800BBDA0u;
static const uint32_t APE_COLLISION_LIST_HEAD = 0x800B20D8u;
static const uint32_t APE_COLLISION_LIST_TAIL = 0x800B20DCu;
static const uint32_t APE_SPIKE_OBJECT = 0x800EC1E0u;

enum {
    APE_HITBOX_COUNT = 128,
    APE_HITBOX_SIZE = 0x38,
    APE_COLLISION_NODE_COUNT = 256,
    APE_COLLISION_NODE_SIZE = 0x14,
    APE_COLLISION_PLAYER_ATTACK = 6,

    /* Values the swing writes (club attack table entry). */
    APE_CLUB_HIT_RADIUS = 0xB0,
    APE_CLUB_HIT_EFFECT = 0x24,
    APE_CLUB_HIT_FLAGS = 0x204
};

static uint32_t ape_alloc_hitbox(void) {
    uint32_t i, off;
    for (i = 0u; i < APE_HITBOX_COUNT; ++i) {
        if ((int16_t)psx_mod_read_half(APE_HITBOX_FLAGS + i * 2u) < 0) {
            const uint32_t entry = APE_HITBOX_POOL + i * APE_HITBOX_SIZE;
            psx_mod_write_half(APE_HITBOX_FLAGS + i * 2u, 1u);
            /* Same fields 0x8007228C clears. */
            for (off = 0u; off < 0x18u; off += 4u)
                psx_mod_write_word(entry + off, 0u);
            for (off = 0x18u; off < 0x2Cu; off += 2u)
                psx_mod_write_half(entry + off, 0u);
            psx_mod_write_word(entry + 0x2Cu, 0u);
            psx_mod_write_word(entry + 0x30u, 0u);
            return entry;
        }
    }
    return 0u;
}

/* Mirrors 0x80071E44 for a type-6 node: claim a node, then append it. */
static int ape_register_player_attack(uint32_t hitbox) {
    uint32_t i;
    for (i = 0u; i < APE_COLLISION_NODE_COUNT; ++i) {
        const uint32_t node = APE_COLLISION_NODE_POOL + i * APE_COLLISION_NODE_SIZE;
        if ((int16_t)psx_mod_read_half(node) < 0) {
            const uint32_t tail = psx_mod_read_word(APE_COLLISION_LIST_TAIL);
            psx_mod_write_half(node + 2u, (uint16_t)i);
            psx_mod_write_half(node, APE_COLLISION_PLAYER_ATTACK);
            psx_mod_write_word(node + 4u, hitbox);
            psx_mod_write_word(node + 8u, APE_SPIKE_OBJECT);
            psx_mod_write_half(hitbox + 0x2Au, 1u);
            psx_mod_write_word(node + 0xCu, 0u);
            if (psx_mod_read_word(APE_COLLISION_LIST_HEAD) == 0u) {
                psx_mod_write_word(APE_COLLISION_LIST_HEAD, node);
                psx_mod_write_word(node + 0x10u, 0u);
            } else {
                psx_mod_write_word(tail + 0xCu, node);
                psx_mod_write_word(node + 0x10u, tail);
            }
            psx_mod_write_word(APE_COLLISION_LIST_TAIL, node);
            return 1;
        }
    }
    return 0;
}

static void ape_spawn_flying_hitbox(void) {
    const uint32_t hitbox = ape_alloc_hitbox();
    float heading[3];
    int k;

    if (hitbox == 0u)
        return;

    /* Attack direction: the way the club is travelling. */
    for (k = 0; k < 3; ++k)
        heading[k] = g_throw.phase == APE_THROW_OUT ? g_throw.dir[k] : -g_throw.dir[k];

    psx_mod_write_word(hitbox + 0x0u, (uint32_t)(int32_t)g_throw.pos[0]);
    psx_mod_write_word(hitbox + 0x4u, (uint32_t)(int32_t)g_throw.pos[1]);
    psx_mod_write_word(hitbox + 0x8u, (uint32_t)(int32_t)g_throw.pos[2]);
    psx_mod_write_word(hitbox + 0x18u, (uint32_t)(int32_t)(heading[0] * 4096.0f));
    psx_mod_write_word(hitbox + 0x1Cu, (uint32_t)(int32_t)(heading[2] * 4096.0f));
    psx_mod_write_half(hitbox + 0x20u, APE_CLUB_HIT_RADIUS);
    psx_mod_write_half(hitbox + 0x24u, APE_CLUB_HIT_EFFECT);
    psx_mod_write_half(hitbox + 0x26u, APE_CLUB_HIT_FLAGS);

    if (!ape_register_player_attack(hitbox)) {
        /* No node: give the slot back rather than leak it. */
        psx_mod_write_half(APE_HITBOX_FLAGS +
                               ((hitbox - APE_HITBOX_POOL) / APE_HITBOX_SIZE) * 2u,
                           0xFFFFu);
    }
}

/* Runs once per game frame while the club is the held gadget. */
static void ape_club_handler_entry(struct CPUState* cpu, uint32_t address) {
    (void)cpu;
    (void)address;
    if (g_boomerang_mode && ape_throw_active())
        ape_spawn_flying_hitbox();
}
