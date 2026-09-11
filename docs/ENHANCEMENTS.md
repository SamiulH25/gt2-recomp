# GT2 enhancements

Agreed set: resolution scale, 60fps pacing, widescreen, mod packaging.

## Shipped (config-only, verified)

`game.toml`:

| Enhancement | Setting | Status |
|---|---|---|
| GPU renderer | `renderer = "opengl"` | default, verified title + demo race render |
| Internal resolution | `supersampling = 2` | requested; confirmed `internal scale 2x` on NVIDIA (llvmpipe clamps to 1x); in-race shots clean, no texture seams at 2x bilinear |
| MSAA | `antialiasing = true` | on per GL log |
| Texture filtering | `texture_filtering = "bilinear"` | on per GL log |
| Vertex wobble | `geometry_correction = true` | on per GL log |
| Texture warping | `perspective_texturing = true` | on per GL log |
| Audio quality | `[audio] spu_hq = true` | Catmull-Rom resample, cheap |
| PGXP CPU mode | off (default) | 0.5px tolerance default stands; CPU tier needs validation per game (some rely on int truncation) — see PGXP verdict below |

Screenshots: `docs/screenshots/gl-baseline.png` (vanilla GL) vs
`docs/screenshots/gl-enhanced.png` (this set) — same attract demo, not
pixel-comparable (different moments) but both render correctly with visibly
cleaner fences/billboards under bilinear+AA.

`pgxp_cpu_mode`, Vulkan (`offer_vulkan`), CRT/scanline filters: left default
until validated per game.

## Phase B verdicts (2026-09-08)

- **PGXP: SAFE, benefit unquantified — stays default-off.** Dirt-rally reel
  renders flawlessly with `psx.enhancement.pgxp` on (no explosions, dust/
  trees/billboards correct at 16:9). No VRAM effect by construction
  (byte-identical 320x240 dumps on/off — PGXP lives in the GTE→GPU
  projection path, visible only in supersampled present). Same-frame GL
  A/B deferred: needs paired `present_shot`s at an identical guest frame
  (protocol: poll `frame` in a tight loop, capture at fixed N in both
  runs — determinism makes frames comparable; the f22342 pgxp shot lost
  its mate when the control run died at ~27k). Generated code carries
  PGXP hooks (1422 in shard 00). Decision: ship default-off until a
  same-frame pair shows a visible win. GTE stats showed no pgxp-specific
  counters (`nproj/nsat/nflat/nintpl` only), so pixels are the metric.
- **Fast-loading: NO measurable effect on boot path — stays default-off.**
  Clean back-to-back headless pair (quiet CPU, same binary, only the
  feature flag differs): OFF boot→f6000 = 85s, ON (default 4x, log
  confirms `mod selected 4x load acceleration (0 release frames)`) = 85s.
  (An earlier 40s ON reading under CPU contention was noise/page-cache
  warmth — discarded.) Rationale: headless runs unpaced, so host-pacing
  acceleration has nothing to shorten; the knob could still matter in
  paced windowed play, but that needs its own measurement. `disc_speed`
  (guest-visible) stays off regardless — timing-bug risk, needs
  race-completion regression first.
- **Cheats (`[[recompiler.patch]]`): pipeline proven in principle, no
  addresses RE'd.** Needs instruction (not data) addresses with `expected`
  guards — e.g. credits writes need the storing instruction found via
  `mem_words`/`wtrace` hunt in garage state (blocked on nav). Deferred to
  Phase C/D. No patches ship.
- **Controller:** `default_mode = digital` stands. No DualShock presentation
  policy without an input-sampling audit (Xvfb eats keyboard; SIO auto-input
  owns testing). Deferred.
- **Bezel:** needs owner-selected art + launcher flow; margins are already
  filled with real geometry at 16:9 (no black bars to cover). Deferred.
- **Texture seams:** clean at 2x bilinear on NVIDIA in all in-race shots
  (fences, billboards, guardrails). The 4x-seam question stays open (needs
  `supersampling = 4` run + hires compare).

## Widescreen (mod-owned — active, verified in-race 2026-09-08)

The framework gates widescreen behind trusted mod plugins on PSX
(`game.toml` aspect is clamped: *"widescreen is mod-owned on PSX"*).

**Active now (runtime-only, no regen):**

- `game.toml [video] aspect_ratio = "16:9"` — GTE X-squash + stretched present;
  world geometry keeps correct proportions.
- `game.toml [widescreen] hud_sprt_squash = true` — untagged SPRTs (screen-space
  HUD/menus) center-squash so dialog borders match.
- `game.toml [widescreen] sprite_anchor_addr = "0x1F800070"` — scratchpad
  location for tagged-prim anchors (Tomba convention; semantics unverified
  for GT2, see below).
- `src/mods/gt2_widescreen.c` — registers `psx_mod_register_function_entry_plugin`
  at the 4 gt2_01 projection-funnel PCs. The runtime fires this hook on BOTH
  generated code and the interpreter dispatch (the path overlays actually run
  on), so sprite tags work without a recompiler regen. Each callback guards on
  the function's prologue word to disambiguate overlay members sharing VRAM
  `0x80010000`, then calls `psx_ws_sprite_tag()` directly.

**In-race verification (attract-demo reel, Xvfb GL, 2026-09-08):**

- `gpu_state`: `configured=1 active=1 game_mode=1 x_margin=53 squash=[3,4]`,
  `gte_verts` ~5k/frame — squash genuinely engaged in drive phase.
- `docs/screenshots/demo-race-16x9-chase.png` (1280x720 `present_shot`,
  Lap 2/2 2nd): scene fills the 16:9 frame edge-to-edge (guardrail left,
  trees/cars right) — NO right-margin cutoff, NO edge void, NO pop-in.
  Scissor fix holds in-race; no game-side outcode cull defect observed.
  HUD (`2nd` badge, Lap/TV text) renders at native proportions via
  `hud_sprt_squash`.
- Sprite tags: **never fire in the demo-reel path** — 0 `[GT2_WS]`
  callbacks in 300s, funnel PCs absent from both `[gt2_phot]` static
  histogram (17M hits) and 570-entry `dirty_ram_stats` interp table
  (4.9M blocks). Tags are inert-but-harmless; world-correct output above
  comes from the GTE pre-squash + stretched present alone. Retag
  candidates (`func_80027BBC/80028394`, the actual hot per-object PCs)
  REJECTED: neither touches scratchpad `0x1F800070` and neither preserves
  `$a0` as a prim pointer (reused as scratch) — no Tomba-shaped anchor
  exists there. See `docs/WIDESCREEN_RE.md`.
- Demo determinism: same guest frame = byte-identical pixels across runs
  (f11400 md5 match) — all future A/B comparisons are rigorous.

Remaining RE (see `docs/WIDESCREEN_RE.md` for static findings + verification
plan, `tools/ghidra/LoadGT2Overlays.py`, gt2-reversing symbols):

1. **Sprite-tag runtime load address — ANSWERED for demo path, OPEN for
   player-drive.** The 4 candidates never execute in the attract reel
   (static + interp + callback evidence, 2026-09-08). They may still serve
   player-drive/garage paths (untested — nav blocked). Options: re-probe
   via `dirty_ram_stats` once nav reaches drive, or leave tags inert
   (output is already world-correct without them).
2. **Anchor semantics verification.** Scratchpad `0x1F800070` UNVERIFIED — the
   stored value may be a pointer (`sp+0x20`) rather than SXY in 2 of the 4
   functions. Confirm an SXY store after RTPS/RTPT before trusting tags.
   Verify by reading `0x1F800070` at tag-fire time and checking it's in
   screen-X range (0..320). Moot until a firing path is found.
3. **Screen-edge cull sites** — NO defect observed at 16:9 in-race
   (`demo-race-16x9-chase.png`, x_margin 53 filled with real geometry).
   `WIDESCREEN_RE.md` still has NO static candidates (GPU auto-clip assumed).
   No `cull.keep` entries needed; global nocull stays diagnostic-only.
4. After tags verify: adaptive (up to 21:9) + per-element HUD tuning.

## 60fps pacing (wall + guest content both measured 60Hz)

Wall-clock pacer holds 60Hz: 280s Xvfb windowed run (`--renderer opengl`,
`GT2_AUTO_INPUT=1`), 273 `[FPS]` samples, min 59.6 / mean 60.4 (first
sample 185 = pre-pacer burst), zero drops below 55; `present cadence:
wall-clock pacer (16.68ms)`, `host refresh unknown`, GL `internal scale 2x`
pipeline (confirmed engaged on NVIDIA 2026-09-08: `GL GPU pipeline ready
(internal scale 2x)`; the `supersampling 1x` resolve note below is
llvmpipe-only).
Run reached frame 16575 (track-pick phase), overlay `58b40c17` loaded.

**Guest content rate — VERDICT 2026-09-08: 60fps guest (new frame every
vblank).** Build-debug TCP during demo drive: `frame_fingerprint count 256
frame_lo 7300` → 256/256 unique `wr_hash`, 0 consecutive repeats;
`vblank_rate` → paced raise 7372 / delivered 7199 / pollhack 0 (no
~96/s poll-fallback bug).

`frame_interpolation` NOT needed for content rate (optional only for
>60Hz panels). `vsync` IS game.toml-settable (`on`/`off`/`adaptive`) —
user's panel is 75Hz vs 59.94 guest; leave default.

## Mod packaging (workflow established)

- Declarative (`[[patch]]`/`[[overlay]]` + `expected` guards) for memory/disc
  mods once addresses are RE'd; format-5 `[[plugin]]` for native plugins
  (no arbitrary `.so` — only statically registered implementations).
- Builtins available launcher-side (all default off): `psx.enhancement.pgxp`,
  `psx.enhancement.fast-loading`, `psx.enhancement.cd-speed`,
  `psx.presentation.bezel`.
- GT2's first package: `mods/gt2-widescreen/` (active, above).
