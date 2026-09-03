/* GT2 widescreen enhancement plugin (game-owned, default off).
 *
 * Activation path: mods/gt2-widescreen manifest (format 5, [[plugin]] id
 * "gt2.widescreen") -> mod_register_activation_plugin -> this callback runs
 * after the launcher's final mod-plan commit, before renderer init.
 *
 * Current state: ASPECT ONLY (16:9 GTE X-squash + stretched present). Correct
 * world proportions; HUD/menus stretch until sprite tags land.
 *
 * TODO (needs GT2 RE):
 *  - ws_sprite_tag_funcs: guest addrs of per-prim functions ($a0 = prim ptr)
 *    for cars/billboards, + ws_sprite_anchor_addr (scratchpad SXY), so tagged
 *    sprites re-squash around their projected anchor at GP0 submission.
 *  - cull sites: functions doing screen-edge culling that must learn the
 *    wider frustum (see gt2-reversing splat symbols + psx_ws_func_has_screen_cull).
 *  - Set hud_sprt_squash equivalent once sprite tagging is in.
 */
#include "mod_plugins.h"

#define GT2_WS_PLUGIN_ID "gt2.widescreen"

static void gt2_widescreen_activate(void) {
    /* Fixed 16:9. Adaptive (up to 21:9) once cull sites are mapped. */
    psx_mod_set_fixed_display_aspect(16, 9);
}

PSX_MOD_CONSTRUCTOR(gt2_register_widescreen_plugin) {
    psx_mod_register_activation_plugin(GT2_WS_PLUGIN_ID,
                                       gt2_widescreen_activate);
}
