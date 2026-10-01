import os
src = "src/mods/ape_boomerang_club_plugin.c"
dst = "src/mods/ape_power_up_gadgets_plugin.c"
s = open(src).read()

def sub(old, new, count=1):
    global s
    assert s.count(old) == count, (s.count(old), old[:70])
    s = s.replace(old, new)

sub("""/*
 * Boomerang Stun Club.
 *
 * Pressing the Stun Club's face button while the club is already equipped
 * toggles boomerang mode.""", """/*
 * Power Up Gadgets.
 *
 * Pressing an equipped gadget's face button again powers it up (and again
 * powers it down). Each power-up is its own feature on the Mods page with its
 * own plugin id, so the runtime only runs the callbacks of enabled ones.
 *
 * Boomerang Stun Club: pressing the Stun Club's face button while the club is
 * already equipped toggles boomerang mode.""")

sub("""static uint8_t g_previous_face_buttons;
static uint8_t g_last_face_button;
static uint8_t g_boomerang_mode;
static uint32_t g_glow_frame;

/* Read by the throw logic. */
int ape_boomerang_mode_active(void) {
    return g_boomerang_mode;
}
""", """/* Second-press power toggle for one gadget. */
typedef struct ApePowerToggle {
    uint8_t gadget;
    uint8_t on;
    uint8_t previous_face_buttons;
    uint8_t last_face_button;
    uint32_t glow_frame;
} ApePowerToggle;

static ApePowerToggle g_club_power = { 0u /* APE_GADGET_STUN_CLUB */ };
#define g_boomerang_mode (g_club_power.on)
""")

sub("""static uint8_t ape_find_club_slot(void) {
    uint8_t slot;
    for (slot = 0u; slot < APE_FACE_SLOT_COUNT; ++slot) {
        if (ape_read_slot(slot) == APE_GADGET_STUN_CLUB)
            return slot;
    }
    return APE_INVALID_SLOT;
}

static int ape_club_is_held(void) {
    return psx_mod_read_byte(APE_HELD_GADGET_ADDRESS) == APE_GADGET_STUN_CLUB;
}""", """static uint8_t ape_find_gadget_slot(uint8_t gadget) {
    uint8_t slot;
    for (slot = 0u; slot < APE_FACE_SLOT_COUNT; ++slot) {
        if (ape_read_slot(slot) == gadget)
            return slot;
    }
    return APE_INVALID_SLOT;
}

static int ape_gadget_is_held(uint8_t gadget) {
    return psx_mod_read_byte(APE_HELD_GADGET_ADDRESS) == gadget;
}

static int ape_club_is_held(void) {
    return ape_gadget_is_held(APE_GADGET_STUN_CLUB);
}""")

# Glow: draw for a given toggle.
sub("""static uint32_t ape_glow_pulse(void) {
    const uint32_t half = APE_GLOW_PERIOD_FRAMES / 2u;
    const uint32_t t = g_glow_frame % APE_GLOW_PERIOD_FRAMES;""", """static uint32_t ape_glow_pulse(uint32_t glow_frame) {
    const uint32_t half = APE_GLOW_PERIOD_FRAMES / 2u;
    const uint32_t t = glow_frame % APE_GLOW_PERIOD_FRAMES;""")
sub("""static void ape_draw_club_glow(uint8_t slot) {""", """static void ape_draw_power_glow(uint8_t slot, uint32_t glow_frame) {""")
sub("""    const uint32_t pulse = ape_glow_pulse();""", """    const uint32_t pulse = ape_glow_pulse(glow_frame);""")

# Generic toggle update replaces the club-only VBlank body.
start = s.index("static void ape_reset_toggle_state(uint8_t pressed_face_buttons) {")
end = s.index("/* ---- Throw ----")
s = s[:start] + """/*
 * Second press of the held gadget's own button, with that gadget already in
 * hand from an earlier press, toggles its power-up. Switching away (or losing
 * the gadget, or leaving gameplay) powers it down. Returns 1 when the toggle
 * changed state this call; *slot_out receives the gadget's face slot.
 */
static int ape_update_power_toggle(ApePowerToggle* toggle, uint8_t* slot_out) {
    const uint8_t was_on = toggle->on;
    uint8_t pressed_face_buttons, just_pressed, slot;

    *slot_out = APE_INVALID_SLOT;
    if (!psx_mod_game_started() || !ape_is_playable()) {
        toggle->on = 0u;
        toggle->previous_face_buttons = 0u;
        toggle->last_face_button = 0u;
        return was_on;
    }

    pressed_face_buttons = ape_pressed_face_buttons();
    just_pressed = (uint8_t)(pressed_face_buttons &
                             (uint8_t)~toggle->previous_face_buttons);
    toggle->previous_face_buttons = pressed_face_buttons;

    slot = ape_find_gadget_slot(toggle->gadget);
    if (slot == APE_INVALID_SLOT || !ape_gadget_is_held(toggle->gadget)) {
        toggle->on = 0u;
        if (just_pressed != 0u)
            toggle->last_face_button = just_pressed;
        return was_on;
    }
    *slot_out = slot;

    if (toggle->last_face_button == 0u)
        toggle->last_face_button = ape_face_button_from_slot(slot);

    if (just_pressed != 0u) {
        const uint8_t face = ape_face_button_from_slot(slot);
        if (just_pressed == face && toggle->last_face_button == face) {
            toggle->on = (uint8_t)!toggle->on;
            toggle->glow_frame = 0u;
            if (toggle->on)
                ape_play_arm_sfx();
        }
        toggle->last_face_button = just_pressed;
    }

    if (toggle->on) {
        ++toggle->glow_frame;
        ape_draw_power_glow(slot, toggle->glow_frame);
    }
    return toggle->on != was_on;
}

static void ape_end_throw(void);
static void ape_update_throw(void);

static void ape_boomerang_club_vblank(void) {
    uint8_t club_slot;

    ape_update_arm_sfx();
    if (ape_update_power_toggle(&g_club_power, &club_slot) && !g_club_power.on)
        ape_end_throw();
    if (g_club_power.on)
        ape_update_throw();
}

""" + s[end:]

open(dst, "w", newline="").write(s)
os.remove(src)
print("refactored ->", dst)
