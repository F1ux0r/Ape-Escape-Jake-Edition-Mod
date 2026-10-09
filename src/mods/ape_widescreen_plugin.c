#include "mod_plugins.h"
#include "gpu.h"

/* The original top-level state dispatcher uses halfword 800F4470 == 0x0E
 * for the card menu. Its decorative projections fool GTE-based gameplay
 * detection; preserve the composed 4:3 layout while gameplay stays wide. */
static int ape_native_menu(void) {
    return psx_mod_read_half(0x800F4470u) == 0x0Eu;
}

/*
 * Ape Escape's projection, culling, UI, and dome hooks remain framework
 * configuration. These trusted activation callbacks let the game-owned mod
 * package choose how those hooks are presented before renderer startup.
 */
static void ape_widescreen_16_9_activate(void) {
    gpu_ws_set_native_scene_predicate(ape_native_menu);
    (void)psx_mod_set_fixed_display_aspect(16u, 9u);
}

static void ape_widescreen_21_9_activate(void) {
    gpu_ws_set_native_scene_predicate(ape_native_menu);
    (void)psx_mod_set_fixed_display_aspect(21u, 9u);
}

static void ape_widescreen_adaptive_activate(void) {
    gpu_ws_set_native_scene_predicate(ape_native_menu);
    (void)psx_mod_set_fixed_display_aspect(16u, 9u);
    (void)psx_mod_set_adaptive_display_aspect(21u, 9u);
}

PSX_MOD_CONSTRUCTOR(ape_register_widescreen_plugins) {
    (void)psx_mod_register_activation_plugin(
        "ape.widescreen.16-9", ape_widescreen_16_9_activate);
    (void)psx_mod_register_activation_plugin(
        "ape.widescreen.21-9", ape_widescreen_21_9_activate);
    (void)psx_mod_register_activation_plugin(
        "ape.widescreen.adaptive", ape_widescreen_adaptive_activate);
}
