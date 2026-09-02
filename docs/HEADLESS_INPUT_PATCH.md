# Headless auto-input patch for GT2 license -> main menu

Patched `psxrecomp/runtime/src/main.cpp:sample_headless_pad_into_sio` to auto-press buttons after game_started.

Logic:
- Uses `s_frame_count` and `fntrace_is_game_started()`
- f 300-3000: START (0xFFF7) every 180 frames for 12 frames -> gets past Sony/license screens
- f 3000-6000: CROSS (0xBFFF) similarly -> enters main menu
- f >=6000: alternate START/CROSS

Build: only main.cpp recompiled (3s), shards cached. No need for PSX_DEBUG_TOOLS.

Overlay handling verified: GT2.OVL at 0x80165000 loads after ~19k frames, confirmed via psx_last_run_report.json ram_peeks not zero.

Disc staging fix: `/tmp/gt2_combined/disc/*.bin` was missing after cmake; fixed via symlink to `disc/Gran Turismo 2...bin` (660M). Root cause: cmake POST_BUILD only copied game.toml, not disc bin (disc is gitignored, so build dir disc is stale). Future fix: add `add_custom_command` to copy bin, or ensure `disc/` is populated before configure.

Next: add periodic PC logging and framebuffer dump to confirm main menu visually.
