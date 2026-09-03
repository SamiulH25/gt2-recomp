# GT2 widescreen RE — render funnel + sprite-tag candidates

Status: static analysis done (2026-09-02). Runtime verification pending.
Parent: `docs/ENHANCEMENTS.md` ("Widescreen — scaffolded, RE pending").

## Result

`gt2_01` (OVL member 1, 316920 bytes decompressed) is the 3D render overlay:
1314 GTE ops vs 51/8/0/1/2 in `gt2_02..06`, 1239 in SCUS. Four per-object
projection funnels use the exact Tomba-style slot — `lui $at,0x1F80` +
`sw $reg,0x70($at)` (scratchpad `0x1F800070`), object pointer in `$a0`,
then GTE perspective transform. This byte pair occurs exactly 4x in all of
SCUS + 6 overlays, all inside these 4 functions (gt2_01 file-VRAM `0x80010000`):

| Func (gt2_01 VRAM) | Prologue word | Anchor store | GTE |
|---|---|---|---|
| `0x8001C17C` | `27BDBFC0` (sp `-0x4040`) | `0x8001C1BC` | RTPS `4A180001` @ `0x8001C1E4`, ~238 COP2 in window |
| `0x800234F8` | `27BDDFB8` (sp `-0x2048`) | `0x8002353C` | same body shape as `0x8001C17C` (LOD/duplicate?) |
| `0x80019B58` | `27BDFFF0` (sp `-0x10`) | `0x80019B70` | RTPT `4A280030` @ `0x80019BFC` + `mtc2` vertex block |
| `0x8002106C` | `27BDFFC8` (sp `-0x38`) | `0x8002109C` | RTPT @ analogous site + `mtc2` vertex block |

GTE op census in gt2_01: RTPS `0x4A180001` x66, RTPT `0x4A280030` x26
(92 projection sites total), `0x4A780010` x24.

## Negative results (constrain the design)

1. **No classic screen-extent cull funnel.** No function in SCUS or any
   overlay contains the `slti/sltiu` W+H pair (`0x140`/`0x141` + `0xE0`/`0xF1`)
   that `psx_ws_func_has_screen_cull` (`psxrecomp/runtime/src/gpu.c:1485`)
   detects. SCUS has 2 stray `slti` `0x140` hits (loader-area false
   positives); gt2_01 has zero `0x140` immediates at all.
2. **Post-projection checks are depth/validity, not screen pixels.** In the
   64 words after each of the 92 RTPS/RTPT sites, `slti(u)` immediates are
   `0x1000` x28 (GTE SZ-range/validity), `0x0481` x4, misc small — no
   320/640-class width. GT2 most likely relies on GPU auto-clip for
   per-vertex reject (free under widescreen) — same as many PSX titles.
3. **Anchor semantics UNVERIFIED.** In `0x80019B58`/`0x8002106C`, scratch
   `0x70` is also read back as a vertex-table base (`lw $v1,0x70($t4)`), and
   in `0x8001C17C`/`0x800234F8` the stored value is `sp+0x20` (a pointer),
   not obviously an SXY value. Tomba stores the projected SXY value there;
   GT2 may store a pointer and write SXY later in the function (tails not
   fully decoded — capstone stops at the COP2 function words). Do NOT copy
   Tomba's `sprite_anchor_addr` blindly: confirm an SXY-value store to
   `0x1F800070` after RTPS/RTPT before wiring tags.

## Open questions

1. **Runtime load address.** Candidates assume gt2_01 VRAM `0x80010000`
   (splat). At runtime the A9000+ overlay region (`0x80165000` signature
   `58b40c17`, see `docs/BOOT_ATTEMPT.md`) is observed loaded; whether
   gt2_01 executes at `0x8001xxxx` (overwriting SCUS text base) or relocated
   is TBD via SCUS loader disasm (`0x8001146C` `memcpy`, `tools/disasm.py:23`)
   + `dirty_ram_stats` PC histogram in-race.
2. **Overlay aliasing.** All 6 members claim VRAM `0x80010000`
   (`docs/OVERLAYS.md:14`) — one resident at a time. Tag callbacks at these
   PCs must guard on resident identity (e.g. verify prologue word
   `27BDBFC0`/`27BDDFB8`/`27BDFFF0`/`27BDFFC8` before tagging) or they will
   mis-tag when another member is resident.
3. **World-space object culling TBD.** If 16:9 shows pop-in, the culprit is
   a camera-relative object window (Tomba `FUN_80022E44` analogue), not a
   screen funnel. Find via `ws_census` + `gp0_ring` in-race at 4:3, then map
   immediates. No static candidates yet.
4. **HUD/SPRT path.** Untagged textured rects center-squash by default;
   `hud_sprt_squash` + per-element tuning comes after tags verify.

## Verification plan (runtime, no regen)

1. In-race 4:3 capture: `ws_census on`, `gp0_ring` dump, `dirty_ram_stats`
   — confirm the 4 PCs execute per-frame in race/drive scenes and record
   `$a0` + `read_word(0x1F800070)` at entry (log-only probe, no behavior
   change).
2. Decode each func tail past the RTPS/RTPT for an SXY-value store to
   `0x1F800070` (`swc2`/`sdc2`/`sw` after `mfc2` SXY). If present, anchor
   semantics confirmed.
3. Confirm load address: match executing PCs against `0x8001xxxx` vs
   relocated copies.

## Implementation sketch (after verification)

- Runtime-only path, no regen: `game.toml [widescreen] sprite_anchor_addr =
  "0x1F800070"` (runtime global, `psxrecomp/runtime/src/main.cpp:11465`) +
  mod-owned aspect stays in `src/mods/gt2_widescreen.c`.
- Tags via `psx_mod_register_function_entry_plugin` at the 4 PCs (fires on
  both generated code AND interpreter `dirty_ram_interp.c:2877` — the path
  overlays actually execute on), callback guards prologue word, then calls
  `psx_ws_sprite_tag(cpu)`. Needs a tiny framework addition: no mod API
  exposes sprite-tag today (`psxrecomp/runtime/include/mod_plugins.h` has
  only aspect/rate setters) — either export the tag call for trusted
  plugins or add `psx_mod_register_sprite_tag_func(addr)`.
- Cull sites: only if pop-in observed (world-space hunt above).
- Then: adaptive up to 21:9 + `hud_sprt_squash`, Xvfb screenshot A/B
  (`docs/screenshots/`) before enabling by default (stays `default_enabled
  = false` until then).

## Repro

```
isoinfo -R -i /tmp/opencode/gt2.iso -x '/GT2.OVL;1' > /tmp/opencode/GT2.OVL
python3 tools/split_ovl.py   # -> /tmp/opencode/ovl_split/gt2_0{1..6}.exe
# anchor-pair + GTE census: see shell history 2026-09-02 (capstone scans)
```
