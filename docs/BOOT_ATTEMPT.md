# Boot attempt 2026-09-02

Binary: `Gran_Turismo_2_Recompiled` 213M at /tmp/gt2_combined, 359 shards, OpenBIOS

Run: `/tmp/gt2_combined/Gran_Turismo_2_Recompiled` via `xvfb-run` + `headless`

Result: Boot past BIOS LLE, 864 frames (~14 sec at 60fps), then crash at PC=0x00000000
- psx_last_run_report.json: frame 864, last_store_pc 0x8008C090, epc 0x8007C558, ra 0x8007AB3C, overlay 0x80165000 all zero, loads 0
- Overlay loader: valid_count 0, loads 0, inprogress 0 - suggests disc not found, no overlay captured
- Likely cause: disc path resolution when running from /home/... vs /tmp/gt2_combined, cue not found, so GT2.OVL not loaded -> jump to 0x80165000 fails -> null PC

Next fix: run from build dir CWD, ensure disc/ relative to exe, verify libchdr can read MODE2/2352 bin.
