p = "src/mods/ape_boomerang_club_plugin.c"
s = open(p).read()

def sub(old, new):
    global s
    assert s.count(old) == 1, old[:60]
    s = s.replace(old, new)

sub(""" * Arming boomerang mode replays it about three semitones higher on a voice
 * whose envelope is silent, so music and other effects are not cut off.
 */""", """ * Arming boomerang mode replays it about three semitones higher.
 *
 * The game's libsnd voice allocator (0x8009DAB4) keeps a 0x66-byte record
 * per voice at 0x800E07F0: +0x00 non-zero = in use, +0x0A = steal priority,
 * +0x1A non-zero = never steal (set on the game's own looping effects). The
 * voice is claimed there for as long as the sound plays, so music cannot
 * take it and cut the sound off, then handed back.
 */""")

sub("""static const uint32_t SPU_NOISE_LOW = 0x1F801D94u;
""", """static const uint32_t SPU_NOISE_LOW = 0x1F801D94u;
static const uint32_t APE_SND_VOICE_RECORDS = 0x800E07F0u;
""")

sub("""    APE_SWITCH_SFX_VOLUME = 0x2579
};""", """    APE_SWITCH_SFX_VOLUME = 0x2579,

    APE_SND_VOICE_RECORD_SIZE = 0x66,
    APE_SND_IDLE_PRIORITY = 0xFFFF,
    /* Hold at least this long before trusting a silent envelope, and give
     * the voice back after the cap regardless. */
    APE_ARM_SFX_MIN_FRAMES = 6,
    APE_ARM_SFX_MAX_FRAMES = 120
};

static int g_sfx_voice = -1;
static uint32_t g_sfx_frames;""")

sub("""static int ape_find_silent_voice(void) {
    int voice;
    /* Highest first: the driver hands out low voices first. */
    for (voice = SPU_VOICE_COUNT - 1; voice >= 0; --voice) {
        const uint32_t base = SPU_VOICE_BASE + (uint32_t)voice * SPU_VOICE_STRIDE;
        if (psx_mod_read_half(base + 0xCu) == 0u)
            return voice;
    }
    return -1;
}""", """static uint32_t ape_snd_record(int voice) {
    return APE_SND_VOICE_RECORDS + (uint32_t)voice * APE_SND_VOICE_RECORD_SIZE;
}

static uint16_t ape_voice_envelope(int voice) {
    return psx_mod_read_half(SPU_VOICE_BASE + (uint32_t)voice * SPU_VOICE_STRIDE +
                             0xCu);
}

/* A voice the driver considers free and that is actually silent. */
static int ape_find_silent_voice(void) {
    int voice;
    /* Highest first: the driver hands out low voices first. */
    for (voice = SPU_VOICE_COUNT - 1; voice >= 0; --voice) {
        if (psx_mod_read_half(ape_snd_record(voice)) == 0u &&
            ape_voice_envelope(voice) == 0u)
            return voice;
    }
    return -1;
}

static void ape_claim_voice(int voice) {
    const uint32_t record = ape_snd_record(voice);
    psx_mod_write_half(record + 0x00u, 1u);
    psx_mod_write_half(record + 0x1Au, 1u);
    g_sfx_voice = voice;
    g_sfx_frames = 0u;
}

static void ape_release_voice(void) {
    if (g_sfx_voice >= 0) {
        const uint32_t record = ape_snd_record(g_sfx_voice);
        /* Only undo our own claim; the driver may have moved on already. */
        if (psx_mod_read_half(record + 0x1Au) == 1u &&
            psx_mod_read_half(record + 0x0Au) == APE_SND_IDLE_PRIORITY) {
            psx_mod_write_half(record + 0x1Au, 0u);
            psx_mod_write_half(record + 0x00u, 0u);
        }
    }
    g_sfx_voice = -1;
}

/* Called every VBlank: give the voice back once the sound has finished. */
static void ape_update_arm_sfx(void) {
    if (g_sfx_voice < 0)
        return;
    ++g_sfx_frames;
    if ((g_sfx_frames > APE_ARM_SFX_MIN_FRAMES &&
         ape_voice_envelope(g_sfx_voice) == 0u) ||
        g_sfx_frames > APE_ARM_SFX_MAX_FRAMES)
        ape_release_voice();
}""")

sub("""static void ape_play_arm_sfx(void) {
    const int voice = ape_find_silent_voice();
    uint32_t base;

    if (voice < 0)
        return;
    base = SPU_VOICE_BASE + (uint32_t)voice * SPU_VOICE_STRIDE;
""", """static void ape_play_arm_sfx(void) {
    int voice;
    uint32_t base;

    ape_release_voice();
    voice = ape_find_silent_voice();
    if (voice < 0)
        return;
    ape_claim_voice(voice);
    base = SPU_VOICE_BASE + (uint32_t)voice * SPU_VOICE_STRIDE;
""")

# Tick the release every VBlank, including outside gameplay.
sub("""    uint8_t club_slot;

    if (!psx_mod_game_started() || !ape_is_playable()) {""", """    uint8_t club_slot;

    ape_update_arm_sfx();

    if (!psx_mod_game_started() || !ape_is_playable()) {""")

open(p, "w", newline="").write(s)
print("applied")
