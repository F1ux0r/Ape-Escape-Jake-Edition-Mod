p = "src/mods/ape_boomerang_club_plugin.c"
s = open(p).read()
s = s[:s.index("/* Spike's world position (s16 x, y, z)")].rstrip() + "\n"
s = s.replace(""" * Throw/return flight is added on top of this toggle in a later step.
 */""", """ * While armed, a right-stick flick throws the club instead of swinging it:
 * it spins out along the stick direction (camera-relative), curves, then
 * homes back to Spike's hand. The stock swing is suppressed by rewriting its
 * state requests at set_state entry; the flying club is drawn by rewriting
 * the camera-space hand matrix at the club draw's entry.
 */""")
s = s.replace("#include <stdint.h>\n", "#include <math.h>\n#include <stdint.h>\n", 1)
s = s.replace("""    if (g_boomerang_mode) {
        ++g_glow_frame;
        ape_draw_club_glow(club_slot);
    }
}""", """    if (g_boomerang_mode) {
        ++g_glow_frame;
        ape_draw_club_glow(club_slot);
        ape_update_throw();
    }
}""")
s = s.replace("""static void ape_reset_toggle_state(uint8_t pressed_face_buttons) {
    g_boomerang_mode = 0u;""", """static void ape_end_throw(void);
static void ape_update_throw(void);

static void ape_reset_toggle_state(uint8_t pressed_face_buttons) {
    g_boomerang_mode = 0u;
    ape_end_throw();""")
s = s.replace("""        /* Switching away from the club (or losing it) disarms the mode. */
        g_boomerang_mode = 0u;""", """        /* Switching away from the club (or losing it) disarms the mode. */
        g_boomerang_mode = 0u;
        ape_end_throw();""")
s = s.replace("""            g_boomerang_mode = (uint8_t)!g_boomerang_mode;
            g_glow_frame = 0u;""", """            g_boomerang_mode = (uint8_t)!g_boomerang_mode;
            g_glow_frame = 0u;
            if (!g_boomerang_mode)
                ape_end_throw();""")
s += open(r"D:/Ape Escape Recomp/modding/re/throw_code.c").read()
open(p, "w", newline="").write(s)

t = open("game.toml", encoding="utf-8").read()
old = '# 0x8009ABF4 = club model draw (a1 = hand world matrix).\nmod_function_entry_funcs = ["0x8009A800", "0x8009ABF4"]'
assert old in t
t = t.replace(old, '# 0x8009ABF4 = club model draw (a0/a1 = hand matrix in camera/world space),\n# 0x80064208 = set_state (a0 = new Spike state).\nmod_function_entry_funcs = ["0x8009A800", "0x8009ABF4", "0x80064208"]')
open("game.toml", "w", encoding="utf-8", newline="").write(t)
print("applied")
