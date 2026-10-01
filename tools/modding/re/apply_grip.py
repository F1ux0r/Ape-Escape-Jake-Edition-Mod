p = "src/mods/ape_power_up_gadgets_plugin.c"
s = open(p).read()


def sub(old, new):
    global s
    assert s.count(old) == 1, old[:70]
    s = s.replace(old, new)


sub("""    float right[3], elbow[3];
} ApeDrawFrame;""", """    float right[3], elbow[3];
    /* Sky Flyer model matrix (camera space) for the AE2 grip overlay. */
    uint8_t flyer_valid;
    float flyer_r[3][3], flyer_t[3];
} ApeDrawFrame;""")

sub("""static ApePowerToggle g_flyer_power = { APE_GADGET_SKY_FLYER };""", """static ApePowerToggle g_flyer_power = { APE_GADGET_SKY_FLYER };
/* Grip block extent along the flyer's spin axis (model x; hand toward -x),
 * and which screen winding counts as facing the camera (+1 / -1). */
static const float APE_FLYER_GRIP_LOW = -24.0f;
static const float APE_FLYER_GRIP_HIGH = 2.0f;
#define APE_FLYER_FACE_SIGN 1
static void ape_draw_flyer_grip(void);
static void ape_flyer_handler_entry(struct CPUState* cpu, uint32_t address);""")

sub("""    (void)ape_update_power_toggle(&g_flyer_power, &slot);
    ape_apply_flyer_model(g_flyer_power.on);""", """    (void)ape_update_power_toggle(&g_flyer_power, &slot);
    ape_apply_flyer_model(g_flyer_power.on);
    if (g_flyer_power.on && ape_gadget_is_held(APE_GADGET_SKY_FLYER) && ape_is_playable())
        ape_draw_flyer_grip();""")

sub("""    (void)psx_mod_register_function_entry_plugin(
        "ape.gadgets.sky-flyer", APE_SPIKE_MOVE_STEP, ape_flyer_move_entry);
}""", """    (void)psx_mod_register_function_entry_plugin(
        "ape.gadgets.sky-flyer", APE_SPIKE_MOVE_STEP, ape_flyer_move_entry);
    (void)psx_mod_register_function_entry_plugin(
        "ape.gadgets.sky-flyer", 0x8009B1C0u, ape_flyer_handler_entry);
}""")

s += open(r"D:/Ape Escape Recomp/modding/re/grip_code.c").read()
open(p, "w", newline="").write(s)

t = open("game.toml", encoding="utf-8").read()
assert '"0x80063628"]' in t
t = t.replace('"0x80063628"]', '"0x80063628", "0x8009B1C0"]')
t = t.replace("# 0x80063628 = Spike move step", "# 0x8009B1C0 = Sky Flyer handler (model matrix at 0x800E9E40).\n# 0x80063628 = Spike move step")
open("game.toml", "w", encoding="utf-8", newline="").write(t)
print("applied")
