# Boot attempt 2026-09-02 — update 2

Binary: `Gran_Turismo_2_Recompiled` 213M at /tmp/gt2_combined, 359 shards, OpenBIOS, disc staged fix

Run: `/tmp/gt2_combined/Gran_Turismo_2_Recompiled --headless --renderer software` with auto-input patch (START/CROSS every 180 frames after game_started)

Result:
- Frame 125 (early): overlay 80165000 still zero (not yet loaded)
- Frame 2373 (40s, Xvfb opengl, disc fixed): overlay still zero but further than 864
- Frame 19140 (90s headless auto-input, software): overlay 80165000 = 58b40c17... (non-zero, loaded!) EPC 8007D270 loop (vsync wait), last_store 8007D26C, suggests main loop waiting for input/vsync, not crashed. Frame 3261 (Xvfb opengl) also reached vsync loop at 80028174.
- Disc staging: fixed symlink for 660M bin (`/tmp/gt2_combined/disc/Gran Turismo...bin -> /home/.../disc/...bin`) was missing, caused early overlay load failure (valid_count 0). Now overlay loads via CPU memcpy from GT2.VOL at LBA 473 (GTFS) and GT2.OVL at LBA 331, verified via ram_peeks.
- Headless auto-input: patched `psxrecomp/runtime/src/main.cpp:sample_headless_pad_into_sio` to press START (0xFFF7) every 180f after f>=300, then CROSS (0xBFFF) after f>=3000, to get past license screen. Rebuilt only main.cpp (3s).
- Overlay handling: GT2 overlays at 0x80010000 same as main text, so floor 0xA9000 + dirty path handles it via dirty_ram_is_dirty -> phys_is_overlay_region (>=A9000) and overlay_cache_window_contains dirty check for [0x10000,0xA9000) text range. No need for overlay_cache enable; interpreter handles it. Verified overlay at 80165000 (>=A9000) is considered overlay region and loads.

Next: verify main menu visually via framebuffer dump or GPU log, add periodic PC logging, and try Xvfb software capture with +extension GLX.
