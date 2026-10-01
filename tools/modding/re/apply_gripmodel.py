p = "src/mods/ape_power_up_gadgets_plugin.c"
s = open(p).read()


def sub(old, new):
    global s
    assert s.count(old) == 1, old[:70]
    s = s.replace(old, new)


# 1. Reshape the hub-bar box (v4..v11) into the grip block and recolour it.
sub("""        /* Pale, see-through blades. */""", """        /*
         * Grip: the hub bar is a small box (v4..v11: x 12/16-17, y -28/24,
         * z -10/-5) drawn and depth-sorted by the game itself, so reshaping
         * it into the AE2 grip block keeps it correctly behind or in front
         * of Spike. Its faces take colour-table entries 0..3 and 5..7.
         */
        for (i = 4; i <= 11; ++i) {
            const int16_t* v = g_flyer_model.prop[i];
            ape_set_vertex(prop, i,
                           v[0] <= 13 ? APE_FLYER_GRIP_LOW : APE_FLYER_GRIP_HIGH,
                           v[1] > 0 ? APE_FLYER_GRIP_HALF_Y : -APE_FLYER_GRIP_HALF_Y,
                           v[2] <= -8 ? APE_FLYER_GRIP_Z_LOW : APE_FLYER_GRIP_Z_HIGH);
        }
        /* Pale, see-through blades. */""")
sub("""        g_flyer_model.colour4 = psx_mod_read_word(g_flyer_model.colours + APE_FLYER_BLADE_COLOUR * 4u);
        psx_mod_write_word(g_flyer_model.colours + APE_FLYER_BLADE_COLOUR * 4u,
                           (g_flyer_model.colour4 & 0xFF000000u) | APE_FLYER_BLADE_RGB);""", """        for (i = 0; i < 8; ++i)
            g_flyer_model.colour[i] = psx_mod_read_word(g_flyer_model.colours + (uint32_t)i * 4u);
        psx_mod_write_word(g_flyer_model.colours + APE_FLYER_BLADE_COLOUR * 4u,
                           (g_flyer_model.colour[4] & 0xFF000000u) | APE_FLYER_BLADE_RGB);
        for (i = 0; i < 8; ++i) {
            if (i != APE_FLYER_BLADE_COLOUR)
                psx_mod_write_word(g_flyer_model.colours + (uint32_t)i * 4u,
                                   (g_flyer_model.colour[i] & 0xFF000000u) | APE_FLYER_GRIP_RGB[i]);
        }""")
sub("""            psx_mod_write_word(g_flyer_model.colours + APE_FLYER_BLADE_COLOUR * 4u, g_flyer_model.colour4);""",
    """            for (i = 0; i < 8; ++i)
                psx_mod_write_word(g_flyer_model.colours + (uint32_t)i * 4u, g_flyer_model.colour[i]);""")
sub("""    uint16_t tpage[APE_FLYER_BLADE_QUADS];
    uint32_t colour4;
} g_flyer_model;""", """    uint16_t tpage[APE_FLYER_BLADE_QUADS];
    uint32_t colour[8];
} g_flyer_model;""")

# grip extents and colours (replacing the overlay-only constants)
sub("""/* Grip block extent along the flyer's spin axis (model x; hand toward -x),
 * and which screen winding counts as facing the camera (+1 / -1). */
static const float APE_FLYER_GRIP_LOW = -46.0f;
static const float APE_FLYER_GRIP_HIGH = 2.0f;
#define APE_FLYER_FACE_SIGN 1
static void ape_draw_flyer_grip(void);""", """/* Grip block, in the flyer's model space (spin axis x, hand toward -x). */
static const float APE_FLYER_GRIP_LOW = -46.0f;
static const float APE_FLYER_GRIP_HIGH = 6.0f;
static const float APE_FLYER_GRIP_HALF_Y = 15.0f;
static const float APE_FLYER_GRIP_Z_LOW = -24.0f;
static const float APE_FLYER_GRIP_Z_HIGH = 10.0f;
/* Colour-table entries for the grip faces (index 4 is the blades): silver
 * shades on 0..3, blue on 5..7, so the faces blend silver into blue.
 * Words are 0x00BBGGRR. */
static const uint32_t APE_FLYER_GRIP_RGB[8] = {
    0x00B0ACA8u, 0x00C8C4C0u, 0x00D8D4D0u, 0x00A09C98u,
    0x00000000u, 0x00C07040u, 0x00D88850u, 0x00A86030u
};
/* How far toward the fingertips the flyer sits from the wrist anchor. */
static const float APE_FLYER_FINGER_SHIFT = 14.0f;""")

# 2. Drop the overlay grip: no draw call, and remove its drawing code.
sub("""    if (g_flyer_power.on && ape_gadget_is_held(APE_GADGET_SKY_FLYER) && ape_is_playable())
        ape_draw_flyer_grip();
""", "")
a = s.index("/* Model point -> screen through the frame's flyer matrix. */")
b = len(s)
s = s[:a]
s = s.replace("""/*
 * The AE2 flyer is held by a grip block under a silver spindle, with a red
 * cap over the hub. The stock model has none of that, so it is drawn on top:
 * boxes in the flyer model's own space (blades in the y-z plane at x 12..17,
 * spinning about x, the hand toward -x), transformed by the flyer matrix the
 * handler draws with (0x800E9E40, camera space, captured per frame buffer).
 * Only the faces turned toward the camera are drawn.
 */""", """/*
 * The flyer is drawn by its handler through the hand matrix at 0x800E9E40
 * (camera space). The handler hook poses that matrix before the draw: the
 * AE2 carry (pointing down) and the shift from the wrist into the hand.
 */""")

# 3. Pose: always slide toward the fingertips; carry turn only while folded.
sub("""    if (carry <= 0.0f)
        return;
    ape_read_affine(APE_FLYER_MODEL_MATRIX, &m);
    ape_camera_affine(&cam);
    if (!ape_affine_inverse(&cam, &cam_inv))
        return;""", """    ape_read_affine(APE_FLYER_MODEL_MATRIX, &m);
    ape_camera_affine(&cam);
    if (!ape_affine_inverse(&cam, &cam_inv))
        return;
    /* From the wrist into the hand: along the forearm (elbow -> hand). */
    if (g_spike_hands.valid) {
        float fw[3], fv[3];
        for (i = 0; i < 3; ++i)
            fw[i] = g_spike_hands.right_world[i] - g_spike_hands.right_elbow_world[i];
        ape_normalize3(fw);
        for (i = 0; i < 3; ++i) {
            fv[i] = 0.0f;
            for (k = 0; k < 3; ++k)
                fv[i] += cam.r[i][k] * fw[k] * APE_FLYER_FINGER_SHIFT;
        }
        for (i = 0; i < 3; ++i)
            m.t[i] += fv[i];
    }
    if (carry <= 0.0f) {
        ape_write_affine(APE_FLYER_MODEL_MATRIX, &m);
        return;
    }""")
open(p, "w", newline="").write(s)
print("applied")
