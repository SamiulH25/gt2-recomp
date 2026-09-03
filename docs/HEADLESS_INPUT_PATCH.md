# Headless auto-input + VRAM dump patch for GT2 menu flow

Upstream file: `psxrecomp/runtime/src/main.cpp` (kept as local modification;
preserved in `tools/patches/psxrecomp-gt2-headless.patch`, verified to apply
cleanly to upstream `HEAD` and reproduce the tested source byte-for-byte, so
fresh clones can re-apply with `git apply tools/patches/...`).

## Why SIO-level input

X11 keyboard does not reach the game under Xvfb (no window manager, no input
focus), and the runtime's low-latency re-sample overwrites pad slot 0 with
the idle keyboard every frame — run windowed tests with
`PSX_LOW_LATENCY_INPUT=0`. The patch drives `sio_set_pad_state_slot(0, ...)`
directly, active with `--headless` or `GT2_AUTO_INPUT=1`.

## Auto-input phases (guest frames)

Active-low pad bits: START=`0x0008`, CROSS=`0x4000`, UP=`0x0010`,
LEFT=`0x0080`. Gated on `fntrace_is_game_started()`.

| Frames | Action |
|---|---|
| <120 | nothing (pre-game) |
| 120-3000 | CROSS dense taps (12 of every 36f) — license screens + title while the highlight is still on Start Game (it drifts to Replay Theater if idle) |
| 3000-7000 | CROSS dense taps — card check / new-game dialogs |
| 7000-11000 | settle (garage screen) |
| 11000-13000 | UP then LEFT taps — navigate to arcade icon row |
| 13000-15000 | CROSS taps — select arcade icon |
| 15000-17000 | CROSS taps — pick first car |
| 17000-19000 | CROSS taps — pick first track |
| 19000-21000 | CROSS taps — confirm / start race |
| 21000-60000 | CROSS held (gas) + UP nudges — drive |

Dense 12/36 duty (instead of sparse 12/200) so wall-clock CD timing can't
strand the game between taps. Proven: garage at 45s wall-clock.

## VRAM dump (`gt2_headless_dump_vram`)

Enabled with `GT2_AUTO_DUMP=1` in headless mode. Writes `gt2_dump_<frame>.png`
to the cwd using `gpu_get_display_info` + `gpu_display_pixel_rgb` +
`png_write_rgb`:

- Starts at frame 6000 and only after `fntrace_is_game_started()`
- Every 400 frames through 60000, every 1200 after

## Run (silent — does not touch host audio)

```
SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 DISPLAY=:98 <binary> --renderer software
```

Note: binary must live in a user-writable dir — mods state is
`<exe_dir>/mods/state.toml`, and `build/mods` in the repo is root-owned.
