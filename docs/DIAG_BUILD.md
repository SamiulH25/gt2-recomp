# Diagnostic build + TCP iteration loop

Release (`build/`) ships lean: `PSX_DEBUG_TOOLS=OFF` means no TCP debug
server. For iteration (screenshots, census, profilers), use the separate
debug flavor — same source, extra diagnostics, still optimized:

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPSX_DEBUG_TOOLS=ON \
  "-D_psx_have_SDL3_SDL_h__SDL3__SDL3=1"
cmake --build build-debug -j$(nproc)
```

The `-D_psx_have_...=1` seed works around a try_compile scoping quirk in
`psxrecomp/runtime/runtime.cmake` (`_psx_header_compiles` with an imported
target fails at test-project generation on this host). The check itself is
sound — the same check passes in `build/` with the same compiler and system
SDL3 — so seeding the known answer is safe. If upstream fixes the check,
drop the seed.

Run (separate display; port flag avoids clashing with other sessions):

```bash
Xvfb :97 -screen 0 1280x720x24 &
DISPLAY=:97 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 PSX_LOW_LATENCY_INPUT=0 \
GT2_AUTO_INPUT=1 ./build-debug/Gran_Turismo_2_Recompiled --game game.toml \
--renderer opengl --debug-port 4370
```

Then (default port 4370; `--port` overrides):

```bash
python3 psxrecomp/tools/debug_client.py --port 4370 frame
python3 psxrecomp/tools/debug_client.py --port 4370 screenshot /tmp/shot.png
python3 psxrecomp/tools/debug_client.py --port 4370 ws_census on   # then off
```

Useful raw commands (via a small socket script — debug_client only wraps a
subset): `phase_profile`, `phase_hot` (native + `{"set":"static"}`),
`dirty_ram_stats`, `frame_fingerprint`, `vblank_rate`, `screenshot_hires`
(compositor output incl. aspect fit), `present_shot`.

Rules: release `build/` stays the perf-truth binary (no debug overhead);
`build-debug/` is gitignored and never shipped. CI (`.github/workflows/ci.yml`)
covers patch-script coherence only — game builds need user disc images.
