p = "src/mods/ape_boomerang_club_plugin.c"
s = open(p).read()

def sub(old, new):
    global s
    assert s.count(old) == 1, old[:60]
    s = s.replace(old, new)

sub("""static void ape_end_throw(void);
static void ape_update_throw(void);
""", """/* ---- Arm sound --------------------------------------------------------- */

/*
 * The stock gadget-switch sound, captured from the SPU on every switch:
 * sample 0x2C4E0, pitch 0x02E9, ADSR 0x80FF/0x5FC0, volume 0x2579 L/R.
 * Arming boomerang mode replays it about three semitones higher on a voice
 * whose envelope is silent, so music and other effects are not cut off.
 */
static const uint32_t SPU_VOICE_BASE = 0x1F801C00u;
static const uint32_t SPU_KEY_ON_LOW = 0x1F801D88u;
static const uint32_t SPU_FM_LOW = 0x1F801D90u;
static const uint32_t SPU_NOISE_LOW = 0x1F801D94u;

enum {
    SPU_VOICE_COUNT = 24,
    SPU_VOICE_STRIDE = 0x10,

    APE_SWITCH_SFX_ADDRESS = 0x2C4E0,
    APE_SWITCH_SFX_PITCH = 0x02E9,
    APE_SWITCH_SFX_ADSR_LOW = 0x80FF,
    APE_SWITCH_SFX_ADSR_HIGH = 0x5FC0,
    APE_SWITCH_SFX_VOLUME = 0x2579
};

/* 2^(3/12): three semitones up. */
static const float APE_ARM_SFX_PITCH_SCALE = 1.1892f;

static int ape_find_silent_voice(void) {
    int voice;
    /* Highest first: the driver hands out low voices first. */
    for (voice = SPU_VOICE_COUNT - 1; voice >= 0; --voice) {
        const uint32_t base = SPU_VOICE_BASE + (uint32_t)voice * SPU_VOICE_STRIDE;
        if (psx_mod_read_half(base + 0xCu) == 0u)
            return voice;
    }
    return -1;
}

static void ape_clear_voice_bit(uint32_t reg_low, int voice) {
    const uint32_t reg = reg_low + (voice >= 16 ? 2u : 0u);
    const uint16_t bit = (uint16_t)(1u << (voice & 15));
    psx_mod_write_half(reg, (uint16_t)(psx_mod_read_half(reg) & (uint16_t)~bit));
}

static void ape_play_arm_sfx(void) {
    const int voice = ape_find_silent_voice();
    uint32_t base;

    if (voice < 0)
        return;
    base = SPU_VOICE_BASE + (uint32_t)voice * SPU_VOICE_STRIDE;

    ape_clear_voice_bit(SPU_FM_LOW, voice);
    ape_clear_voice_bit(SPU_NOISE_LOW, voice);
    psx_mod_write_half(base + 0x0u, APE_SWITCH_SFX_VOLUME);
    psx_mod_write_half(base + 0x2u, APE_SWITCH_SFX_VOLUME);
    psx_mod_write_half(base + 0x4u,
                       (uint16_t)((float)APE_SWITCH_SFX_PITCH *
                                  APE_ARM_SFX_PITCH_SCALE + 0.5f));
    psx_mod_write_half(base + 0x6u, (uint16_t)(APE_SWITCH_SFX_ADDRESS >> 3));
    psx_mod_write_half(base + 0x8u, APE_SWITCH_SFX_ADSR_LOW);
    psx_mod_write_half(base + 0xAu, APE_SWITCH_SFX_ADSR_HIGH);
    psx_mod_write_half(SPU_KEY_ON_LOW + (voice >= 16 ? 2u : 0u),
                       (uint16_t)(1u << (voice & 15)));
}

static void ape_end_throw(void);
static void ape_update_throw(void);
""")

sub("""            g_boomerang_mode = (uint8_t)!g_boomerang_mode;
            g_glow_frame = 0u;
            if (!g_boomerang_mode)
                ape_end_throw();""", """            g_boomerang_mode = (uint8_t)!g_boomerang_mode;
            g_glow_frame = 0u;
            if (g_boomerang_mode)
                ape_play_arm_sfx();
            else
                ape_end_throw();""")

sub(""" * toggles boomerang mode. While it is on, the club's HUD icon pulses with an
 * additive glow so the player can see the mode is armed.""", """ * toggles boomerang mode. Arming it plays the gadget-switch sound pitched up,
 * and while it is on the club's HUD icon pulses with an additive glow so the
 * player can see the mode is armed.""")

open(p, "w", newline="").write(s)
print("applied")
