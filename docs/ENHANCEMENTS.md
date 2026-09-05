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

## Widescreen (mod-owned — active, sprite tags pending verification)

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

Remaining RE (see `docs/WIDESCREEN_RE.md` for static findings + verification
plan, `tools/ghidra/LoadGT2Overlays.py`, gt2-reversing symbols):

1. **Sprite-tag runtime load address.** The 4 candidates are gt2_01's
   compile-time VRAM `0x80010000`-based addresses. At runtime the overlay may
   be relocated (the `0x80165000` overlay region is observed loaded). The
   plugin registers compile-time addrs; if tags never fire at runtime, apply
   the runtime offset. Verify via `dirty_ram_stats` PC histogram in-race.
2. **Anchor semantics verification.** Scratchpad `0x1F800070` UNVERIFIED — the
   stored value may be a pointer (`sp+0x20`) rather than SXY in 2 of the 4
   functions. Confirm an SXY store after RTPS/RTPT before trusting tags.
   Verify by reading `0x1F800070` at tag-fire time and checking it's in
   screen-X range (0..320).
3. **Screen-edge cull sites** for the wider frustum — `WIDESCREEN_RE.md` has
   NO static candidates (GT2 relies on GPU auto-clip for per-vertex reject,
   free under widescreen). If 16:9 shows pop-in, it's a world-space object
   cull (camera-relative window), not a screen funnel.
4. After tags verify: adaptive (up to 21:9) + per-element HUD tuning.

## 60fps pacing (pacer measured; guest content rate — measurement procedure defined)

Wall-clock pacer holds 60Hz: 280s Xvfb windowed run (`--renderer opengl`,
`GT2_AUTO_INPUT=1`), 273 `[FPS]` samples, min 59.6 / mean 60.4 (first
sample 185 = pre-pacer burst), zero drops below 55; `present cadence:
wall-clock pacer (16.68ms)`, `host refresh unknown`, GL `internal scale 2x`
pipeline with `supersampling 1x` resolve under llvmpipe (see shipped table).
Run reached frame 16575 (track-pick phase), overlay `58b40c17` loaded.

**Guest content rate — measurement procedure:**

The pacer reports wall-clock fps, but GT2 may render a NEW frame every vblank
(60fps guest content) or every OTHER vblank (30fps guest, each shown twice).
Method via the TCP debug server (port 4370):

1. Launch with `PSX_FPS_TELEMETRY=1` (enables `[FPS]` stderr line) and
   `GT2_AUTO_INPUT=1` to reach a race.
2. During drive phase: `{"cmd":"frame_fingerprint","count":256}` — dumps the
   per-frame RAM-write hash ring. Identical `wr_hash` on consecutive frames =
   repeated frame (30fps guest in 60fps present). Unique `wr_hash` every
   frame = 60fps guest content.
3. Cross-check with `{"cmd":"vblank_rate"}` — confirms the guest receives
   exactly 60 VBlanks/s (not the ~96/s poll-fallback bug).
4. Optional: `present_shot` pairs (staging-only, PNG) for visual A/B.

If races turn out sub-60: `frame_interpolation` is mod-owned (same gate as
widescreen) — needs a mod plugin, not config. `vsync` IS game.toml-settable
(`on`/`off`/`adaptive`) — user's panel is 75Hz vs 59.94 guest; leave default
until race-fps data exists.

## Mod packaging (workflow established)

- Declarative (`[[patch]]`/`[[overlay]]` + `expected` guards) for memory/disc
  mods once addresses are RE'd; format-5 `[[plugin]]` for native plugins
  (no arbitrary `.so` — only statically registered implementations).
- Builtins available launcher-side (all default off): `psx.enhancement.pgxp`,
  `psx.enhancement.fast-loading`, `psx.enhancement.cd-speed`,
  `psx.presentation.bezel`.
- GT2's first package: `mods/gt2-widescreen/` (active, above).
