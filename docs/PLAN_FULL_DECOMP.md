# GT2: Full Roadmap to Full Decomp

This is the complete plan from today (WORKLOG 2026-09-08 — 12 tests PASS,
`recomp untouched`, menu-stable recomp + incremental `decomp/`) to a full
native decomp where `generated/` is no longer needed.

Methodology that works — keep it for every phase below:

* Clean-room C11 in `decomp/` (`decomp/README.md`): read MIPS to learn
  behavior, write C from scratch, provenance comments with `SCUS 0x8001xxxx`,
  never paste assembly.
* `tools/mips_emu.py` + per-system driver (`vol_walk.py`, `ovl_load.py`,
  `car_index.py`, `q2_cache.py`, `car_logo.py`, `task_tail.py`,
  `crs_asset.py`, `boot_chain.py`) + `tools/xcheck_paths.py` C==ref, 0 fails.
  Every new port must have emu ground truth + `decomp/tests/test_*.c` +
  `ctest --test-dir build/decomp -V`.
* `recomp untouched` until Phase G. Recomp stays the playable harness;
  `decomp/` stays host-side and side-car.

---

## 0. Where we are

### 0.1 Recomp (playable)

* `game.toml`: SCUS-94488 Sim v1.2, `exe=disc/SCUS_944.88`,
  `load=0x80010000`, `entry=0x8005D600`, `text_size=0x99000`,
  `strict=true`, `seeds/ghidra_funcs.txt` 1179 entries.
* `generated/`: 358 shards, 51M lines, 1593 funcs, 31535 blocks,
  2033 loops, dispatch 3863 entries (`docs/RECOMP.md`).
  `generated/overlays/overlays_static.c` — all 6 OVL members baked static
  (1.45M lines, gt2_01 3D engine native).
* `CMakeLists.txt`: `gt2` stub + `decomp/` + `gt2-psxrecomp`
  (`PSX_RECOMP_UI/WIZARD/NETPLAY/REWIND OFF`), `src/mods/*.c` glob static
  link, POST_BUILD stages `game.toml`, `disc/*.bin/*.cue/SCUS_944.88`,
  `mods/gt2-widescreen`.
* Status `README.md`, `docs/BOOT_ATTEMPT.md`, `docs/SAVE_STATUS.md`: menu
  stable 26k+ frames, garage renders, GL/audio batch 1 shipped, cards format
  + pass card check, no end-to-end GT2 save yet.

Modification ceiling today:

* L0 free HLE: renderer, supersampling, AA, bilinear, geometry, SPU HQ,
  PGXP, pacer, interpolation, bezel, fast-load.
* L1 declarative `[[recompiler.patch]]` / `[[widescreen.cull.*]]`
  single-word + `[[plugin]]` format-5.
* L2 trusted plugins `src/mods/gt2_widescreen.c`,
  `src/mods/gt2_batch_jobs.c` — activation/function_entry/vblank +
  guest R/W + aspect/interp/disc-speed.
* L3 generated-C surgery `tools/patch_overlay_*.py`,
  `tools/patch_scus_nocull.py` — LOD, nocull experiment,
  prof/gatemeter, parked P2 batch.
* L4 framework fork `tools/patches/psxrecomp-gt2-*.patch`.
* Hazards: CPS resume jumps past fresh-entry hooks, overlay aliasing
  all-`0x80010000`, anchor `0x1F800070` holds `$a0` pointer not SXY in 2/4
  funnels, no screen-cull funnel (GPU auto-clip), runtime region
  `0x80165000` vs compile-time `0x8001xxxx`.

### 0.2 Decomp (19 tests, ~5100 lib lines + ~3800 test lines)

`decomp/CMakeLists.txt`: libs `gt2_cd/vol/iso/ovl/save/spu/mcd/boot/car/
asset/task/host/tim/menu/dispatch/render/card` + exes `test_vol/vol_pack/
tim/cd/iso/ovl/save/spu/mcd/boot/car/sysclock/asset/task/host/menu/
dispatch/render/card` + probes `car_probe/path_probe`.

| Module | State |
|---|---|
| `gt2_cd` `decomp/src/cd/cd.c` | ✅ raw-2352/cooked, `read_sector/pread` |
| `gt2_vol` `decomp/src/vol/vol.c` | ✅ 11581 files / 11568 names / 11620 slots @VOL+0xB800, flat+tree, `span`, raw parity |
| `gt2_iso` `decomp/src/iso/iso.c` | ✅ flat-root `GT2.OVL;1` 331/289752, `GT2.VOL;1` 473/488241152 |
| `gt2_ovl` `decomp/src/ovl/ovl.c` | ✅ container byte-exact 6 members; loader mapped (setjmp/longjmp `0x8007AD58/AD90`, checkpoint `0x8005D9F0`, mailbox `0x801C945C`, staging `0x800A8D5C`→VRAM `0x80010000`, gunzip chain, entry `+0xC0`); dispatch `0x801EF610` = self-registration CLOSED |
| `gt2_save` `decomp/src/save/crc32.c` | ✅ std CRC32 `0x80083178` |
| `gt2_mcd` `decomp/src/mcd/mcd.c` | ✅ parse/format byte-identical/write/delete/chain/checksums |
| `gt2_spu` `decomp/src/spu/spu.c` | ✅ 24×0x28 `0x80078408` + `wait_idle 0x80078370` limit `0x675BFF`; HW backend open |
| `sysclock` header-only | ✅ `t0^(t1<<4)^(t2<<8)^(t3<<12)` `0x800108C0`, Timer2 `0x248` |
| `gt2_car` `decomp/src/car/car.c` | ✅ `namehash 0x80060924`, `weight_init` FOUND (charset `-0..z`, 62 slots), carobj 1110, `car_find 0x8005D950`, wheel 192, engine 305, logo annotate 1336/27 sorted |
| `gt2_asset` `decomp/src/asset/asset.c` | ✅ `crs_hash rol6 0x80083004`, crsmap 120, window `0x8005D848`, cached `0x8005D8A0` slot6→tbl8 `/.crsinfo 0xFC5`, CRS 126 parse/relocate |
| `gt2_task` `decomp/src/task/task.c` | ✅ b3 5 phases + b4/b5/b6 + helpers; b2 queued/completed (descriptor + heap + DMA, async timing); runner `boot_run` exact vs `tools/boot_chain.py BOOTSTATE` |
| `gt2_boot` `decomp/src/boot/boot.c` | ✅ ISO→VOL→OVL chain + overlay load bytes; execution stays in recomp |
| `gt2_host` `decomp/src/host/host.c` | ✅ vsync counter/wait + timer ticks (Phase E); pad/card/GPU/GTE documented gaps |
| `gt2_tim` `decomp/src/tim/tim.c` | ✅ TIM parse/walk/decode/encode, gunzip-join, TXD (Phase C) |
| `gt2_menu` `decomp/src/menu/menu.c` | ✅ record init + counter + 0x59C emit-tick (Phase F.1a) |
| `gt2_dispatch` `decomp/src/dispatch/dispatch.c` | ✅ mode dispatcher arms/tail/helpers (Phase F.1b); tables stay data |
| `gt2_render` `decomp/src/render/render.c` | ✅ emit_A/B arena appends (Phase F.3a); OT consumption open |
| `gt2_card` `decomp/src/card/card.c` | ✅ card status driver, 8 injected callees (Phase F.1); frame layout needs a real save |

Open per `decomp/docs/*_notes.md`: save-frame layout (needs a real save),
SUB TABLE2 arms + `0x80017A44` cluster (need live tables), lazy
registration + jump-table writers + residency proof (need live game),
OT/DMA consumption (needs packet captures), physics/AI (need codec +
race), logo/champtim/CRS/SEQ consumers, PGXP same-frame A/B, 4x seams.

---

## Phase A — Close the harness gaps (unblocks all profiling/verification)

Goal: reach a real race + prove save + get in-race data. Without this every
perf/cull verdict stays provisional (`WORKLOG.md` Next section).

1. **First real race.** `tools/patches/psxrecomp-gt2-autonav-v2-v6.patch`
   v6 holds+armor only reached My Home hub 1/5 (transition-eaten taps).
   Tasks:
   * Try `GT2_AUTO_DEMO=1` attract-reel first — zero navigation, exercises
     in-race renderer for widescreen/seam/LOD photos.
   * Then fix hub→arcade→car→track→drive: lengthen armored windows, add
     frame-hash sync (wait for VRAM stable before next tap), log
     `[gt2_auto] phase=` + `gt2_dump_*.png` every phase.
   * Verify: `docs/screenshots/` in-race dump + `dirty_ram_stats` shows
     member `74BDB962` resident.
   * Exit: 3 consecutive runs reach drive phase.
2. **In-race profile.** Re-run `tools/patch_overlay_prof.py` +
   `tools/patch_overlay_gatemeter.py` with `PSX_PROFLOG=1 PSX_GATELOG=1
   PSX_SPROF_HZ=100` in drive phase. Today menu-hot is `0x800163C8`+5 in
   `4FC1D21E`, parent `74BDB962` cold. Need: race-phase `[gt2_phot]`
   top-6, `near/far` LOD split, `bypass/reject/class/accept` + outcode
   hist, SIGPROF `func_80094DC8` share in-race. Decide funcB-inline vs
   render-thread only after this. Keep `build/` lean
   (`PSX_DEBUG_TOOLS=OFF`, env-log path); use `build-debug/`
   (`docs/DIAG_BUILD.md`) + `frame_fingerprint`/`vblank_rate`/`ws_census`/
   `gp0_ring` for guest-fps + cull attribution.
3. **End-to-end save proof.** `docs/SAVE_STATUS.md`: complete Sim race or
   Replay save, check `saves/card1.mcd` for `BASCUS-94488` blocks, reboot +
   load. `gt2_mcd` + `test_mcd` synthetic round-trip already ready; need
   real-save frame layout (CRC position, car encoding) to close
   `docs/save_notes.md`.
4. **Guest fps verdict.** `docs/ENHANCEMENTS.md` procedure:
   `frame_fingerprint count 256` (`wr_hash` identical = 30fps guest in
   60fps present) + `vblank_rate` (must be 60, not ~96 poll bug).
   Determines whether `frame_interpolation` mod is needed.

---

## Phase B — Exhaust the recomp ceiling (ship all safe enhancements)

Goal: everything possible without understanding game logic.

1. **Widescreen finish.** `docs/WIDESCREEN_RE.md`,
   `src/mods/gt2_widescreen.c`:
   * Confirm runtime PCs: `ws_census` + `dirty_ram_stats` in-race 4:3 — do
     the 4 `0x8001xxxx` fire per-frame? If not, apply `0x80165000`-region
     offset to registration.
   * Confirm anchor: log `read_word(0x1F800070)` at tag-fire; 0..320 =
     SXY, `0x7FFFxxxx` = pointer → keep current squash-correct behavior,
     document.
   * Cull: if 16:9 pop-in persists after scissor fix `f453519a`,
     attribute via `PSX_NO_FRUSTUM_CULL=1`
     (`tools/patch_scus_nocull.py`) one run, then encode proven sites as
     `[[widescreen.cull.keep]]` / `aspect_cone`, not global nocull. Then
     adaptive up to 21:9 + per-element HUD tuning.
2. **Visual/audio config validation.** Supersampling 2x on real NVIDIA
   (llvmpipe clamps to 1x), PGXP CPU mode per-game validation (0.5px
   tolerance; some games rely on int truncation), texture seams at 4x
   bilinear (open both 2D+3D), `geometry_correction`/
   `perspective_texturing` A/B, `spu_hq` Catmull-Rom cost.
3. **Loading.** `psx_mod_set_load_acceleration` (wall-clock only,
   preserves VBlank/CD deadlines — speedrun-safe with
   `release_frames=0`) vs `psx_mod_set_disc_speed` (guest-visible, can
   expose timing bugs). Ship the former; gate the latter behind a feature
   flag after race-completion regression.
4. **Cheat-style declarative patches.** Once an address is RE'd (money,
   unlocks, timer freeze), encode as `[[recompiler.patch]]` with
   `expected` guard — fail-closed on wrong disc rev. This is the safe
   template for all future single-word tweaks.
5. **Controller/presentation.** `default_mode=digital`; add DualShock
   presentation policy only after input-sampling audit. Bezel for
   pillarbox margins as owner-selected resource.

Exit: `docs/ENHANCEMENTS.md` marks each shipped item verified with
screenshots + fps numbers; nothing CPS-unsafe ships (P2 stays
`PSX_BATCH_*=0` default, inert).

---

## Phase C — Asset pipeline: read → modify → inject (CLOSED 2026-09-08)

Goal: mod content without touching code. All items closed or probed:

1. **VOL read/write — SHIPPED.** `gt2_vol_pack` + `gt2_vol_open_mem` +
   `tools/vol_pack.py` + `test_vol_pack`: zero-rep byte-identical
   (C⊕Python hash-identical), grow/shrink safe, rejections. Table
   carriers (files 0/1) never replaced; degenerate marker preserved.
2. **Texture (TIM/TXD) — SHIPPED core, containers documented.**
   `gt2/tim.h` (parse/walk/decode/encode/`gunzip_join`/logo-locate/TXD)
   + `test_tim` + `tools/tim_dump.py`. arc_topmenu 12-TIM chain
   round-trips; GT-logo decodes. Logo pixel decode + champtim dims need
   overlay consumers (open). TXD span rule recorded for mods.
3. **Car data — probed.** `.carcolor` = 15-bit data, dims open;
   `/carparam` 30 kids + `.cdo.gz` share a non-gzip custom codec;
   decompressor = libpress chain (Phase D target).
4. **Track/course — probed.** CRS to field level with hypotheses;
   mapinfo = 3 names + zero reservation; map binary open.
5. **Audio — probed.** SEQ = raw event stream (`docs/audio_notes.md`);
   player + SPU backend are Phase D.
6. **OVL repack — SHIPPED.** `tools/ovl_pack.py`: byte-identical
   rebuild, member replace + verify, 4-aligned members.

1. **VOL read/write.** Today `gt2_vol` + `tools/vol_dump.py`
   list/find/extract/cook. Add: `gt2_vol_replace` (rebuild `tbl[]`
   offsets, preserve `0x2FC` head + `0xB800` table + `0xBE7C` flat tail),
   `vol_pack.py`, round-trip test (unpack→repack byte-identical when
   unchanged; size-change case relocates tail). This unlocks all data mods.
2. **Texture (TIM/TXD).** Decode `arc_topmenu` (118784 gzip →
   `bg01.tim`), `champtim.tim` 16532, `carlogo` 1673 TIMs,
   `data-race.txd` 44983 via `gt2_tim` module + `tim_dump.py`. Verify by
   re-encoding + in-game screenshot diff. Needed for HD HUD/track
   textures.
3. **Car data.** `gt2_car` already gives index/wheel/engine/logo. Next:
   `carparam` tunes (handling/physics tables — location TBD via Q2 deep
   slots + overlay consumers), `carcolor`, `cdo.gz` model blobs (header +
   decompress, no geometry decode yet). Each needs emu + xcheck vs
   `tools/car_index.py`.
4. **Track/course.** `gt2_asset` gives `/crsmap` + `/.crsinfo` + 126 CRS
   records (offsets rebased). Next: CRS record struct decode (what
   `0x18`-stride fields mean), `course_map`/`course_mapinfo` batch-date
   files, `crsinfo` overlay consumer (linear vs cached —
   `docs/asset_notes.md`). Requires overlay-side RE (Phase D).
5. **Audio.** `spu_10.seq` 5486 + `INST` sample blob, `sound` dir tail
   slots. Add `gt2_seq` parser + playback via host MIDI/wav compare.
6. **OVL repack.** `tools/split_ovl.py` → `overlays/gt2_0*.exe` +
   `gt2_ovl_read_member` byte-exact. Add `ovl_pack.py` (6 gzip members,
   `hdr_size 0x30` + `comp_size/off`) so patched overlays can be tested
   via interpreter path before native regen.

---

## Phase D — Overlay RE (the big unknown: 1.13M of game code)
(CLOSED 2026-09-09 — member census + roles, non-uniform entries,
dispatch endpoints (SCUS passes `$a0`-table, overlay self-registers),
funnel tails (clip+outcode), seeds unchanged by design, codec probed;
record-fill hunt narrowed to Phase E — see `decomp/docs/overlay_notes.md`)

Goal: name every member + entry + hot function; required before any
gameplay decomp.

1. **Member census.** For each of 6 members (sizes `docs/OVERLAYS.md`):
   GTE census (gt2_01 has 1314 vs 51/8/0/1/2 — 3D engine confirmed),
   syscall/COP0/JAL graph via `tools/disasm.py`, string refs (`gt2.ovl`,
   `gt2.vol` at `0x80011D30+`), splat YAML cross-ref (`gt2-reversing`).
   Assign roles (render, menu, race, replay, license, sound?).
2. **Entries + dispatch.** `0x800100C0` task0a stub only holds for boot
   member; gt2_02 starts `2a10a400`. For each member find its init entry
   + self-registration writes to `0x801EF610` (stride `0x14`, valid+8) via
   write-watch replay (`tools/ovl_load.py` pattern). Document calling
   convention (`0x801FF610` arg?).
3. **Render funnel full decode.** 4 funnels `0x8001C17C/0x800234F8`
   (RTPS) + `0x80019B58/0x8002106C` (RTPT) + 92 projection sites (66 RTPS
   + 26 RTPT) + funcB `0x80020110` phase-1 loop + helper `0x80020FD8
   max+med/2+min/4` + parent `0x8002002C` 33-batch + `0x8007B640` outcode
   test. Decode tails past COP2 (capstone stops at function words —
   hand-decode `mfc2/swc2` SXY stores). This closes anchor + cull + LOD
   semantics for good.
4. **Seeds growth.** Promote verified overlay entries into
   `seeds/ghidra_funcs.txt` (today 1179 SCUS-only) + `[[overlays]]`
   captures so `compile_overlays.py` native coverage grows toward 100%.
   Each addition needs `expected`-style evidence (emu hit + prologue +
   cross-ref).

---

## Phase E — Host replacements for boot/sysinit (native boot checklist)
(CLOSED 2026-09-09 — `gt2_host` vsync/timer shims, b2 queue/complete port
with boot_run integration, record-fill hunt bounded (lazy registration or
deeper init; entrypoint needs live game), OT via LIBGPU observation,
`neg`+div-operand harness fixes — see `decomp/docs/task_notes.md`,
`decomp/docs/boot_notes.md`; pad/card/GPU/GTE stay documented gaps)

Goal: `gt2_boot` + `gt2_host` can init without recomp. Order per
`decomp/docs/boot_notes.md` checklist:

1. `iso✅→vol✅→ovl✅→dispatch(mapped)→spu✅→sysclock✅→host→task0b→recomp`.
2. **Already portable, keep:** `vol_span`, `q2_cache_build`, `car`
   family, `asset` family, `task` b3/b4/b5/b6, `crc32`, `mcd`, `spu
   init/wait_idle`, `sysclock_combine`.
3. **New `gt2_host`:** vsync (`0x80010954/0x80010928` — install host
   vsync, spin on `0x80011DF4>=4` → host counter), pad (`0x80087148`
   buffers `0x801F0C98/CBA` → SDL state), card/FS (`0x80086CE8/0x80086D78/
   0x80086DE8` BIOS `A70/AB/B4F/4E/4A` → `gt2_mcd` backend),
   timer/sysclock programming (Timer2 `0x248` → host timer). GPU
   (`0x8007FE34`) / GTE (`0x8008BB10` B(0x56)) / SPU-setup vectors stay
   HW/BIOS-only — document, do not port.
4. **b2 solved properly.** Today docs-only: 3-word descriptor at
   `0x801E2CE0` via `0x80078790` + kick `0x800787CC/0x8006830C` needs CD
   driver (heap `0x80092E74` overlaps descriptor). Port once `gt2_cd`
   read streaming + IRQ model exists in host harness.
5. **CD-init paging.** `0x80011494` PVD/`CD001`/cursor model emulated;
   `gt2_iso` takes robust path. Only revisit if cycle-accurate boot
   needed.
6. **Native `boot_run` harness.** Extend `test_task.c` `boot_run` (today
   Q2→b0→b1→b3→b4→b5→b6→b7 on native buffers) with `gt2_host` timers + b2
   emulated CD bytes → assert BOOTSTATE exact including paged-in slots
   beyond resident 0..1535.

---

## Phase F — Core systems decomp (the long tail)
(CLOSED 2026-09-10 — ports: menu record/tick (`gt2_menu`), menu
dispatcher (`gt2_dispatch`), render packet bodies (`gt2_render`), card
driver (`gt2_card`); live-gated residue below. 19/19 tests PASS.)

Each gets its own `gt2_<sys>.h`, `src/<sys>/`, `docs/<sys>_notes.md`,
`tools/<sys>.py`, `tests/test_<sys>.c`. Rough dependency order
(status 2026-09-10):

1. **Save system.** ✅ driver (`gt2_card_run`, 504 B machine, 8 injected
   callees) + MCD format/write/delete. OPEN: frame layout (SC + CRC32
   tail positions from real save — needs Phase A.3) + car/progress
   encoding (`gt2_save_frame_{encode,decode,verify}` still blocked).
2. **Menu/UI flow.** ✅ record/tick/dispatch ports + live deep-log run
   (nav verdict: garage throughout). OPEN: SUB's 10 TABLE2 arms,
   `0x80017A44` cluster, lazy-registration writers, residency proof.
3. **Render loop.** ✅ emit_A/B bodies + tiling proof (no overlap).
   OPEN: ordering table, OT/DMA consumption, host frame renderer
   (needs live packet streams via `gp0_ring` captures).
4. **Physics/handling.** OPEN — needs carparam codec (Phase D.5) +
   live race.
5. **AI/race rules.** OPEN — needs live race (attract reel is the
   oracle, unmined).
6. **Audio/music.** ✅ SEQ probe + SPU init. OPEN: player/voice
   allocation (needs live trace).
7. **CD streaming.** ✅ slot6/228/229 windows. OPEN: multi-slot
   generalization (only 3 slots observed — speculative).

Each system exit: emu steps with no traps on real bytes + full-table
xcheck + host test PASS + docs open questions narrowed to named consumers.

---

## Phase G — Full decomp integration (retire `generated/`)

1. **Native boot to menu.** `gt2_boot` + `gt2_host` + all Phase E/F
   systems link into a `gt2_native` exe that opens `GT2_ISO`, runs
   `boot_run`, enters menu loop, renders via host GPU (SDL3, same as
   recomp runtime), reads SDL input, writes `gt2_mcd`. No `CPUState`, no
   shards, no interpreter.
2. **Parity gate.** Same headless auto-input phases drive both binaries;
   frame-hash ring + screenshots must match (modulo enhancement deltas).
   `xcheck` harnesses run in CI with `GT2_ISO` present, SKIP otherwise so
   plain builds never break.
3. **Cutover.** `CMakeLists.txt`: `gt2_native` becomes default;
   `gt2-psxrecomp` kept as reference harness for one release, then
   optional. `generated/` removed from gitignore-required to archived.
4. **Modding API.** Declarative VOL/TIM/param swaps (Phase C) become
   first-class `mods/` packages; `src/mods/` plugin ABI frozen
   (activation/function_entry/vblank + guest R/W + aspect/interp/
   disc-speed) so existing widescreen + future mods carry over.

---

## Phase H — What full decomp unlocks (do not attempt before G)

* True 60fps logic (not just present interpolation), native-wide renderer
  (retire squash hack), new cars/tracks/menus, physics rebalance,
  career/economy edits, netplay (`[netplay]` gate already in
  `game.toml`), replay theater export, HD texture packs, debug menus.

---

## Immediate next 5 (concrete)

1. `GT2_AUTO_DEMO=1` in-race screenshot + `[gt2_phot]` histogram — DONE
   (Phase A: demo reel mapped + profiled, screenshots in
   `docs/screenshots/`).
2. `frame_fingerprint` + `vblank_rate` guest-fps verdict — DONE (Phase A.4:
   60fps guest, pollhack 0).
3. Real-save capture → `gt2_save_frame_*` — OPEN, blocked on nav
   (card driver done, MCD IO done; frame layout + car/progress encoding
   need the save itself).
4. `vol_pack.py` + `tim_dump.py` — DONE (Phase C: hash-identical C⊕Python
   round-trips + 12-TIM chain + TXD).
5. `gt2_host` vsync/pad/timer stubs + b2 CD-kick emulation — DONE
   (Phase E: shims + queue/complete port + boot_run integration).

Each step keeps `ctest` 19/19 green and `recomp untouched` until Phase G.
