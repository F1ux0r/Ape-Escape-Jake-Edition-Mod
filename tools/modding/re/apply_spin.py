p = "src/mods/ape_power_up_gadgets_plugin.c"
s = open(p).read()


def sub(old, new):
    global s
    assert s.count(old) == 1, old[:70]
    s = s.replace(old, new)


# Swap the spin-disc object onto the propeller model while powered.
sub("""        g_flyer_model.prop_base = prop;
        g_flyer_model.disc_base = disc;
        g_flyer_model.applied = 1u;""", """        /*
         * In flight (0x83/0x84) the handler draws the spin disc 0x800EC180
         * instead of the propeller. Point the disc object at the propeller's
         * data (header, colours, polygons, vertices: words 0..5) so the AE2
         * propeller and its grip stay visible; it is spun by the pose code.
         */
        for (i = 0; i < 6; ++i) {
            g_flyer_model.disc_words[i] = psx_mod_read_word(APE_FLYER_DISC_OBJECT + (uint32_t)i * 4u);
            psx_mod_write_word(APE_FLYER_DISC_OBJECT + (uint32_t)i * 4u,
                               psx_mod_read_word(APE_FLYER_PROP_OBJECT + (uint32_t)i * 4u));
        }
        g_flyer_model.prop_base = prop;
        g_flyer_model.disc_base = disc;
        g_flyer_model.applied = 1u;""")
sub("""    } else if (!powered && g_flyer_model.applied) {
        if (prop == g_flyer_model.prop_base)""", """    } else if (!powered && g_flyer_model.applied) {
        /* Disc object back onto its own model first. */
        if (psx_mod_read_word(APE_FLYER_DISC_OBJECT) == psx_mod_read_word(APE_FLYER_PROP_OBJECT))
            for (i = 0; i < 6; ++i)
                psx_mod_write_word(APE_FLYER_DISC_OBJECT + (uint32_t)i * 4u, g_flyer_model.disc_words[i]);
        if (prop == g_flyer_model.prop_base)""")
sub("""    uint16_t tpage[APE_FLYER_BLADE_QUADS];
    uint32_t colour[8];
} g_flyer_model;""", """    uint16_t tpage[APE_FLYER_BLADE_QUADS];
    uint32_t colour[8];
    uint32_t disc_words[6];
} g_flyer_model;""")

# The restore path reads the disc's vertices through the object, so restore
# the disc vertices via the saved base rather than re-deriving it.
sub("""        if (disc == g_flyer_model.disc_base)
            for (i = 0; i < APE_FLYER_DISC_VERTS; ++i)""", """        if (g_flyer_model.disc_base != 0u)
            for (i = 0; i < APE_FLYER_DISC_VERTS; ++i)""")
sub("""                ape_set_vertex(disc, i, g_flyer_model.disc[i][0], g_flyer_model.disc[i][1], g_flyer_model.disc[i][2]);
        if (psx_mod_read_word(APE_FLYER_PROP_OBJECT + 8u) == g_flyer_model.prims) {""", """                ape_set_vertex(g_flyer_model.disc_base, i, g_flyer_model.disc[i][0], g_flyer_model.disc[i][1], g_flyer_model.disc[i][2]);
        if (psx_mod_read_word(APE_FLYER_PROP_OBJECT + 8u) == g_flyer_model.prims) {""")

# Spin the propeller about its own axis while it is being spun.
sub("""    if (carry <= 0.0f) {
        ape_write_affine(APE_FLYER_MODEL_MATRIX, &m);
        return;
    }""", """    if (carry <= 0.0f) {
        /* In use: spin the propeller about its axis (model x), M * Rx. */
        const uint32_t state = psx_mod_read_half(APE_SPIKE_OBJECT + 0x5Eu);
        if (state >= APE_STATE_FLYER_FIRST && state <= APE_STATE_FLYER_LAST) {
            float c2, s2, r1[3], r2[3];
            g_flyer_spin += APE_FLYER_SPIN_STEP;
            c2 = cosf(g_flyer_spin); s2 = sinf(g_flyer_spin);
            for (i = 0; i < 3; ++i) {
                r1[i] = m.r[i][1] * c2 + m.r[i][2] * s2;
                r2[i] = -m.r[i][1] * s2 + m.r[i][2] * c2;
            }
            for (i = 0; i < 3; ++i) {
                m.r[i][1] = r1[i];
                m.r[i][2] = r2[i];
            }
        }
        ape_write_affine(APE_FLYER_MODEL_MATRIX, &m);
        return;
    }""")
sub("""static const float APE_FLYER_CARRY_SHIFT = 22.0f;""", """static const float APE_FLYER_CARRY_SHIFT = 22.0f;
/* Propeller spin per game frame while in use (radians). */
static const float APE_FLYER_SPIN_STEP = 1.1f;
static float g_flyer_spin;""")
open(p, "w", newline="").write(s)
print("applied")
