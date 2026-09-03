# Headless auto-input + VRAM dump patch for GT2 menu flow

Upstream file: `psxrecomp/runtime/src/main.cpp` (kept as local modification;
preserved in `tools/patches/psxrecomp-gt2-headless.patch` so fresh clones can
re-apply with `git -C psxrecomp apply ../tools/patches/psxrecomp-gt2-headless.patch`).

## Auto-input (`sample_headless_pad_into_sio`)

Active in headless mode, or in windowed mode with `GT2_AUTO_INPUT=1`.
Gated on `fntrace_is_game_started()`. PSX pad bits are active-low.

| Frames | Action |
|---|---|
| <120 | nothing (pre-game) |
| 120-2400 | START (`~0x0008`) tap 12/200f — advances license screens |
| 2400-4800 | CROSS (`~0x4000`) tap 12/200f — title -> main menu |
| 4800-6000 | settle (garage screen) |
| 6000-7200 | UP 12/120f for 600f, then LEFT 12/120f — navigate to arcade icon |
| 7200-8400 | CROSS 12/120f — select arcade icon |
| 8400-9600 | CROSS 12/120f — pick first car |
| 9600-10800 | CROSS 12/120f — pick first track |
| 10800-12000 | CROSS 12/120f — confirm / start race |

## VRAM dump (`gt2_headless_dump_vram`)

Enabled with `GT2_AUTO_DUMP=1` in headless mode. Writes `gt2_dump_<frame>.png`
to the cwd using `gpu_get_display_info` + `gpu_display_pixel_rgb` +
`png_write_rgb` (already linked in runtime):

- Starts at frame 6000 and only after `fntrace_is_game_started()`
- Every 300 frames for 6000-15000 (menu navigation), every 1200 after
- Skips when display disabled or 0-sized

## Run

```
mkdir -p /tmp/opencode/gt2-run/{disc,bios,build/mods,saves}
cp game.toml /tmp/opencode/gt2-run/
ln -sf <repo>/disc/* /tmp/opencode/gt2-run/disc/
cp <repo>/build/bios/openbios.bin /tmp/opencode/gt2-run/bios/
cp <repo>/build/Gran_Turismo_2_Recompiled /tmp/opencode/gt2-run/
cp -r <repo>/build/mods/packages /tmp/opencode/gt2-run/mods/
cd /tmp/opencode/gt2-run && GT2_AUTO_DUMP=1 ./Gran_Turismo_2_Recompiled --headless --renderer software
```

Note: binary must live in a user-writable dir — mods state is
`<exe_dir>/mods/state.toml`, and `build/mods` in the repo is root-owned.
