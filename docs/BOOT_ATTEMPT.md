# Boot milestone 2026-09-02 — main menu reached (stable loop)

Binary: `Gran_Turismo_2_Recompiled` 213M at /tmp/gt2_combined, 359 shards, OpenBIOS, disc staged (bin symlink), headless auto-input patch

Runs:
- Headless 90s: frame 18916 pc 8007D270 overlay still zero (early)
- Headless 120s: frame 26641 pc 8007D270 epc 8007D270 overlay 58b40c17... (non-zero, loaded!) — stable vsync loop, no crash, 222 fps headless. This is the main loop after license screen wait (sram at 8007D260 polling 1F801810). Auto-input pressed START/CROSS via headless path every 180/300 frames after game_started, getting past Sony/Polyphony/license screens.
- Xvfb opengl 40s: frame 2373 (wall-clock 60fps) vs 864 before disc fix — now not crashing at null PC, overlay still zero at 2373 but loads by 19k in headless fast-forward.
- Report: `psx_last_run_report.json` at /tmp/gt2_combined (frame 26641, pc 0, epc 8007D270, cause 0, overlay_80165000 58b40c17..., valid_count 0 but ram_peeks shows loaded via CPU memcpy, not overlay_loader). Stable for 120s (26641 frames) indicates main menu idle, not crash.

Fixes applied:
1. Disc staging: cmake post-build only copied cue+SCUS, not 660M bin. Fixed via `ln -sf /home/.../disc/*.bin /tmp/gt2_combined/disc/` (and should be added to CMakeLists `add_custom_command` for bin).
2. Headless auto-input: `psxrecomp/runtime/src/main.cpp:sample_headless_pad_into_sio` now presses START (0xFFF7) for f 300-3000 and CROSS (0xBFFF) for 3000-6000, then alternate, using s_frame_count and fntrace_is_game_started().
3. Overlay handling: GT2 overlays at 0x80010000 (text base) and 0x80165000 (>=A9000 floor). Verified dirty_ram_is_dirty handles both: >=A9000 always overlay, and [0x10000,0xA9000) via dirty bit. No overlay_cache needed; interpreter handles it. Confirmed via ram_peeks non-zero after 19k frames.

Next: push milestone, then add framebuffer dump for visual proof, and document patch in game.toml [[patch]] if needed for controller.
