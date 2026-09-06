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

## Auto-input phases (guest frames, v6)

Active-low pad bits: START=`0x0008`, CROSS=`0x4000`, UP=`0x0010`,
LEFT=`0x0080`, RIGHT=`0x0020`. Gated on `fntrace_is_game_started()`.
Phase transitions log as `[gt2_auto] f=<n> phase=<name>`.

| Frames | Action |
|---|---|
| <120 | nothing (pre-game) |
| 120-2400 | START taps (12 of every 200f) — license screens |
| 2400-4800 | CROSS taps — title → garage (skipped in demo mode) |
| 4800-6000 | settle (garage screen) |
| 6000-6400 | hold LEFT — parks arrow on EXIT button |
| 6400-7200 | settle |
| 7200-10200 | CROSS every 150f (~20 clicks) — EXIT → My Home hub |
| 10200-11000 | settle in hub (arrow on big GARAGE) |
| 11000-11400 | hold UP — hub → top icon row (assumed grid) |
| 11400-12000 | settle |
| 12000-12240 | RIGHT x2 — grid → checkered (assumed race entry) |
| 12240-12800 | settle |
| 12800-15200 | CROSS every 150f — assumed area entry |
| 15200-16000 | settle |
| 16000-18400 | CROSS every 150f — assumed car pick |
| 18400-19200 | settle |
| 19200-21600 | CROSS every 150f — assumed track pick |
| 21600-22400 | settle |
| 22400-24800 | CROSS every 150f — assumed confirm/go |
| 24800-60000 | CROSS held (gas) + UP nudges — drive |

Design notes (from screenshot-driven iteration, 2026-09-06):
- The garage cursor is a magnetic arrow, NOT a focus ring. Hotspots hold it
  indefinitely; empty space snaps back to center rest (caught mid-return).
- Holds self-synchronize through load transitions (taps get eaten); select
  windows are stretched to ~16-20 slow clicks (every 150f) for the same reason.
- Proven: boot→garage (every run), garage→EXIT (hold LEFT), EXIT→hub once
  (My Home: GARAGE/GAME STATUS/LICENSER CREDITS), trophy→GAME STATUS once.
  Hub→arcade still open (flaky exit→hub transition is the current blocker).

## Attract-demo mode (`GT2_AUTO_DEMO=1`)

Navigate boot → title, then release all buttons (`demo-idle` phase from
frame 3000): the title demo reel (AI-driven 3D race) plays with zero menu
navigation. For renderer verification (widescreen margins, seams, LOD)
without solving menu flow. Untested whether GT2 runs a reel on idle title.

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
