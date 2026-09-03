# GT2 enhancements

Agreed set: resolution scale, 60fps pacing, widescreen, mod packaging.

## Shipped (config-only, verified)

`game.toml`:

| Enhancement | Setting | Status |
|---|---|---|
| GPU renderer | `renderer = "opengl"` | default, verified title + demo race render |
| Internal resolution | `supersampling = 2` | requested; llvmpipe clamps to 1x (`gr_scale()` fallback), real NVIDIA GPUs take 2x — user should confirm |
| MSAA | `antialiasing = true` | on per GL log |
| Texture filtering | `texture_filtering = "bilinear"` | on per GL log |
| Vertex wobble | `geometry_correction = true` | on per GL log |
| Texture warping | `perspective_texturing = true` | on per GL log |
| Audio quality | `[audio] spu_hq = true` | Catmull-Rom resample, cheap |
| PGXP CPU mode | off (default) | 0.5px tolerance default stands; CPU tier needs validation per game (some rely on int truncation) — candidate follow-up |

Screenshots: `docs/screenshots/gl-baseline.png` (vanilla GL) vs
`docs/screenshots/gl-enhanced.png` (this set) — same attract demo, not
pixel-comparable (different moments) but both render correctly with visibly
cleaner fences/billboards under bilinear+AA.

`pgxp_cpu_mode`, Vulkan (`offer_vulkan`), CRT/scanline filters: left default
until validated per game.

## Widescreen (mod-owned — scaffolded, RE pending)

The framework gates widescreen behind trusted mod plugins on PSX
(`game.toml` aspect is clamped: *"widescreen is mod-owned on PSX"*).
Scaffolded, default off:

- `src/mods/gt2_widescreen.c` — game-owned plugin `gt2.widescreen`,
  activation sets fixed 16:9 (GTE X-squash + stretched present; world correct,
  HUD stretches). Wired into `gt2-psxrecomp` via `src/mods/*.c` glob.
- `mods/gt2-widescreen/1.0.0/manifest.toml` — format-5 manifest, feature
  `widescreen-16x9`, `default_enabled = false`.

Remaining RE (see `docs/WIDESCREEN_RE.md` for static findings + verification plan,
`tools/ghidra/LoadGT2Overlays.py`, gt2-reversing symbols):

1. `ws_sprite_tag_funcs` — guest addrs of per-prim functions ($a0 = prim)
   + `ws_sprite_anchor_addr`, so HUD/sprites re-squash around anchors.
2. Screen-edge cull sites for the wider frustum
   (`psx_ws_func_has_screen_cull` + aspect-cone sites).
3. Then flip to adaptive (up to 21:9) + `hud_sprt_squash` equivalent.

## 60fps pacing (pacer measured; guest content rate pending)

Wall-clock pacer holds 60Hz: 280s Xvfb windowed run (`--renderer opengl`,
`GT2_AUTO_INPUT=1`), 273 `[FPS]` samples, min 59.6 / mean 60.4 (first
sample 185 = pre-pacer burst), zero drops below 55; `present cadence:
wall-clock pacer (16.68ms)`, `host refresh unknown`, GL `internal scale 2x`
pipeline with `supersampling 1x` resolve under llvmpipe (see shipped table).
Run reached frame 16575 (track-pick phase), overlay `58b40c17` loaded.

Unmeasured: guest content rate — whether menus/races render a NEW frame
every vblank (60fps) or repeat (30fps guest). Method: `present_shot` pairs
over TCP 4370 in garage vs drive phases, neighbor-md5 compare. Note
`frame_interpolation` is ALSO mod-owned (same gate as widescreen), so if
races turn out sub-60 it needs a mod plugin, not config. `vsync` IS
game.toml-settable (`on`/`off`/`adaptive`) — user's panel is 75Hz vs 59.94
guest; leave default until race-fps data exists.

## Mod packaging (workflow established)

- Declarative (`[[patch]]`/`[[overlay]]` + `expected` guards) for memory/disc
  mods once addresses are RE'd; format-5 `[[plugin]]` for native plugins
  (no arbitrary `.so` — only statically registered implementations).
- Builtins available launcher-side (all default off): `psx.enhancement.pgxp`,
  `psx.enhancement.fast-loading`, `psx.enhancement.cd-speed`,
  `psx.presentation.bezel`.
- GT2's first package: `mods/gt2-widescreen/` (above).
