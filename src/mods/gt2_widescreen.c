/* GT2 widescreen enhancement plugin (game-owned, default off).
 *
 * Activation path: mods/gt2-widescreen manifest (format 5, [[plugin]] id
 * "gt2.widescreen") -> mod_register_activation_plugin -> this callback runs
 * after the launcher's final mod-plan commit, before renderer init.
 *
 * Aspect ratio is driven by game.toml [video] aspect_ratio = "16:9" — the
 * framework handles GTE X-squash + stretched present. hud_sprt_squash is a
 * runtime global set from game.toml [widescreen].
 *
 * Sprite tags: the 4 gt2_01 projection-funnel functions live in the overlay
 * (not SCUS), so they can't use the recompiler's sprite_tag_funcs emit (that
 * only tags generated SCUS code). Instead we register a function_entry_plugin
 * at each PC — the runtime fires psx_mod_function_entry on BOTH generated
 * code AND the interpreter dispatch (dirty_ram_interp.c:2877), which is the
 * path overlays actually execute on. The callback guards on the prologue word
 * to disambiguate overlay members sharing VRAM 0x80010000, then calls
 * psx_ws_sprite_tag() directly (statically linked into the same binary).
 *
 * Runtime load address caveat: the 4 addresses below are gt2_01's compile-time
 * VRAM 0x80010000 base (from docs/WIDESCREEN_RE.md). At runtime the overlay
 * may be relocated (the A9000+ overlay region is observed at 0x80165000). The
 * plugin registers these addrs; if tags never fire at runtime, the overlay is
 * relocated and the registered PCs need the runtime offset applied. See
 * docs/WIDESCREEN_RE.md §"Runtime load address".
 */
#include <stddef.h>
#include <stdio.h>
#include "mod_plugins.h"

#define GT2_WS_PLUGIN_ID "gt2.widescreen"

/* Declared in runtime/src/gpu.c — not in mod_plugins.h but statically linked. */
extern void psx_ws_sprite_tag(struct CPUState* cpu);

/* gt2_01 projection-funnel entries (compile-time VRAM 0x80010000 base).
 * Each entry: guest PC, prologue word (guard against overlay aliasing). */
static const struct { uint32_t pc; uint32_t prologue; } gt2_ws_tag_sites[] = {
    { 0x8001C17C, 0x27BDBFC0 },  /* sp -0x4040, RTPS @ 0x8001C1E4 */
    { 0x800234F8, 0x27BDDFB8 },  /* sp -0x2048, LOD/duplicate of 0x8001C17C */
    { 0x80019B58, 0x27BDFFF0 },  /* sp -0x10,  RTPT @ 0x80019BFC */
    { 0x8002106C, 0x27BDFFC8 },  /* sp -0x38,  RTPT @ 0x80021128 */
};

static volatile uint32_t s_tag_cb_count = 0;
static volatile uint32_t s_tag_cb_mismatch = 0;

static void gt2_ws_tag_cb(struct CPUState* cpu, uint32_t address) {
    /* Prologue-word guard: disambiguate overlay members sharing VRAM base.
     * Reads the first instruction at the entry PC and compares to the
     * expected prologue. Cheap (main-RAM read), fires once per prim. */
    uint32_t expected = 0;
    for (size_t i = 0; i < sizeof(gt2_ws_tag_sites)/sizeof(gt2_ws_tag_sites[0]); i++) {
        if (gt2_ws_tag_sites[i].pc == address) {
            expected = gt2_ws_tag_sites[i].prologue;
            break;
        }
    }
    if (!expected) return;
    s_tag_cb_count++;
    if (psx_mod_read_word(address) != expected) {
        s_tag_cb_mismatch++;
        if ((s_tag_cb_mismatch & (s_tag_cb_mismatch - 1)) == 0 || s_tag_cb_mismatch <= 4)
            fprintf(stderr, "[GT2_WS] mismatch @ 0x%08X: read 0x%08X exp 0x%08X (total=%u)\n",
                    address, psx_mod_read_word(address), expected, s_tag_cb_mismatch);
        return;
    }
    psx_ws_sprite_tag(cpu);
    if ((s_tag_cb_count & (s_tag_cb_count - 1)) == 0 || s_tag_cb_count <= 8)
        fprintf(stderr, "[GT2_WS] TAG OK @ 0x%08X (count=%u)\n",
                address, s_tag_cb_count);
}

static void gt2_widescreen_activate(void) {
    /* PSX gates widescreen behind a trusted mod: the framework clamps
     * game.toml [video] aspect to 4:3 unless a mod calls this. Fixed 16:9
     * for now; adaptive (up to 21:9) after cull sites are mapped. */
    psx_mod_set_fixed_display_aspect(16, 9);
    /* Register function_entry_plugin at each tag site. The runtime fires
     * psx_mod_function_entry on both generated code and interpreter dispatch,
     * so this works for overlay functions without a regen. */
    for (size_t i = 0; i < sizeof(gt2_ws_tag_sites)/sizeof(gt2_ws_tag_sites[0]); i++) {
        psx_mod_register_function_entry_plugin(
            GT2_WS_PLUGIN_ID,
            gt2_ws_tag_sites[i].pc,
            gt2_ws_tag_cb);
    }
}

PSX_MOD_CONSTRUCTOR(gt2_register_widescreen_plugin) {
    psx_mod_register_activation_plugin(GT2_WS_PLUGIN_ID,
                                       gt2_widescreen_activate);
}
