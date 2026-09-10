# Worklog — GT2 Hybrid Recompilation

Updated: 2026-09-09 (Phase F started)

## Done

- [2026-09-09] Phase F.1a menu record + tick (`gt2/menu.h`, `tools/menu_tick.py`,
  `test_menu` PASS): gt2_02 record init (5 fields, holes kept) + saturating
  0..12 counter (wrap edges emu-proven) + full 0x59C emit_tick port
  (div-3 magic, (12-cnt) blend, flag-selected stanzas via injected
  emit_A/emit_B/emit_S). The test caught 3 real port bugs (extra stanza A,
  `|` vs `+` packet carry, s4-vs-(s4-s5) fill base) — all settled against
  emu hook args/packet bytes, not reasoning. 15 vectors byte-exact.
  Machine-beats-mind count: an armchair div gave 49, emu gave 46 (51*11=561,
  not 591). Emitter protocol decoded (arena [0x801C93EC], tags 1/4, 8+12 B
  packets) = F.3 ground floor. Wrapper inflate pair + 0x8006B61C stay gaps.
- [2026-09-09] Live deep-log run (v6 full, 207900 frames): nav STILL blocked —
  garage at hub-settle/drive/final dumps (`docs/screenshots/garage-at-drive-
  phase.png`); EXIT→hub reproduced 0×. Miss top-6 (0x800177CC…×20058, frozen
  early) = boot-era lockstep set, CPS-continuation class, unattributed
  (needs resident-CRC recorder upgrade); amiss empty all run; phot garage
  set differs from boot-menu set (residency timeline is F.1b).

- [2026-09-09] Phase E finished (host + bounded residue):
  * E.1 b2 + E.2 host (committed): queue/complete port, boot_run
    integration, vsync/timer shims. 15 tests PASS.
  * E.3 record-fill hunt: base inits ruled out one by one (gt2_01: 1
    config byte via proven snapshot-diff; gt2_02: pure clamps; gt2_03:
    lookup dispatcher; gt2_04: memset ignoring table); entrypoint path
    mapped 7876 steps to interrupt-coupled dispatch (needs live game);
    loader MISS path fills nothing. Residue narrowed to lazy
    registration or deeper init, with protocol. lui-0x801F sites read
    the b6 area, not the table.
  * E.4 OT groundwork: no direct GPU regs in gt2_01 (LIBGPU HLE path —
    observe via gp0_ring/ws_census, not static scans).
  * Harness div-operand bug fixed + verified (selftest, xcheck 0 fails,
    BOOTSTATE identical incl. step counts).
  15 tests PASS; recomp untouched

- [2026-09-09] Phase E.1 b2 sys.ins preload (`gt2_task_b2_queue/complete`,
  `tools/b2_kick.py`, boot_run integration): descriptor
  `{0x801E2CF0, 0x200, heap}` + bump `([DST+0x10]+0x1F)&-0x10` exact,
  request `{Q2[247]=/sound/sys.ins → idx 11579, LBA 238855, 34600 B}`
  verified against instrumented emulation (an early analysis was off by
  20 sectors from bad mental hex — settled by hooks, not reasoning);
  queued after b1, completed after b7 (deposit overlaps cache/header
  inputs, so the async timing is load-bearing and preserved). Consumer
  0x80060884 parses 0x40/0x7F units (0x200 header). Needed `neg` in
  `tools/mips_emu.py` (self-test + full boot_chain BOOTSTATE identical).
  A `&~0x10` draft got heap 4127 vs 4128 — the test caught it.
- [2026-09-09] Phase E.2 host backends (`gt2/host.h`, `test_host`):
  vsync counter/wait (spin to >= 4) + timer ticks feeding the ported
  combine; pad/card/GPU/GTE stay documented gaps (SIO init + BIOS
  trampolines disassembled to prove HW-coupling). 15 tests PASS;
  recomp untouched

- [2026-09-09] Phase D finished (overlay RE to evidence limits):
  * D.1 census (committed): 6 member roles + non-uniform entries.
  * D.2 dispatch-fill CLOSED structurally: no overlay code forms
    `0x801EF610`, but two SCUS sites pass it in `$a0` (task0a→overlay
    init `jal 0x80010000`; loader post-CD-read + `(s0<<3)+0xC` stride
    loop) — the overlay fills its own table (callee side). Open residue
    (Phase E): which overlay instructions store (stride 0x14, valid +8).
  * D.3 funnel tails: clip+outcode emission into the scratch packet
    ($a3), outcode bitmask in $a1 (ori 2/4/8/0x10/0x20), then mfc2 SXY.
    Full 92-site + OT chain stays render-loop work (Phase E/F).
  * D.4 seeds: no additions (file is SCUS-only by design; overlay uses
    captures; criteria unmet for all candidates).
  * D.5 codec entry: still open — no asset strings in ovr2/4/5/6 (paths
    come from SCUS descriptors), no static table-forming store found;
    decoder hunt moves to dynamic (write-watch on decode output) or
    data-flow from member inits in Phase E.
  14 tests PASS; recomp untouched (widescreen 2-site already rebuilt)

- [2026-09-09] Phase D.1 overlay census + anchor verdict
  (`tools/ovl_census.py`, `decomp/docs/overlay_notes.md`): roles from
  static census + upstream splat/symbol leads — gt2_01 3D+menu, gt2_02
  REPLAY (entrypoint0 0x80011384 = loader-table idx1 target), gt2_03
  ARCADE+FMV (DecDCTReset), gt2_04 shared race utils (memset_caller at
  entry), gt2_05 event/license/career rules (1163 syms), gt2_06 movie
  player (DecDCT_inout). Entries NON-UNIFORM (kills shared-stub model).
  Anchor semantics instruction-proven: 19B58/2106C store *obj (key,
  taggable), 1C17C/234F8 store sp+0x20 (untaggable) — plugin narrowed
  to the 2 valid sites (never fires anywhere observed; rebuilt).
  Only 4 sw-to-0x70 sites exist in gt2_01 (no post-projection SXY store).
  Codec probe: .cdo.gz/.dat.gz NOT zlib-family (custom codec, entropy
  ~7.7, no TOC) — separate overlay-side decoder, entry open. /tmp wipe
  recovered (ISO re-cooked; evidence had been committed). 14 tests PASS;
  recomp rebuilt (widescreen 2-site)

- [2026-09-08] Phase C finished with the first real mods
  (`tools/demo_mod.py`, self-verifying): TXD swap ("Wrong Way"→
  "FAST WAY!", same span) + TIM0 R/B channel swap with per-member
  re-gzip, packed via `vol_pack` machinery — verified back out of the
  blob (string at file+11, pixel #25271 0x0400→0x0001 after
  VOL→gzip→TIM→pixel round trip). Staged-blob injection only; same-size
  ISO sector overwrite is future work. Remaining opens honestly parked:
  logo/champtim/carcolor pixel dims, carparam/cdo codec (= libpress
  chain, Phase D), CRS semantics, SEQ player — all need overlay
  consumers. 14 tests PASS; recomp untouched

- [2026-09-08] Phase C CLOSED. Asset pipeline read→modify→(staged)inject:
  * C.2 TIM SHIPPED (`gt2/tim.h`, `test_tim`, `tools/tim_dump.py`):
    std TIM parse/walk/decode(4/8/16/24-bit+CLUT)/encode; arc_topmenu =
    12 chained 16-bit TIMs over 12 gzip members at 2048-aligned slots
    (plain inflate stops at member 1 — new `gt2_gunzip_join`; one TIM per
    member verified); TIM0 decodes to the GT logo; round-trip byte-exact.
    Logo containers: head + trailing CLUT TIM located strictly
    (a-a7rl: 5184 + 4-bit/16c 62x56) but declared length exceeds the file
    and offsets vary — pixel decode waits for the overlay consumer.
    champtim.tim = 32 zero bytes + 16500 B opaque (dims open). TXD =
    2293 NUL-padded strings fully indexed (span rule for mods).
  * C.6 OVL repack SHIPPED (`tools/ovl_pack.py`): byte-identical rebuild
    verified; member replace (re-gzip) + verify; members are 4-aligned
    with zero padding (initial tight-pack rebuild failed by 15 B — the
    tool disagreement caught it).
  * C.3 probed: .carcolor = 15-bit color data, dims open; /carparam =
    30 kids in custom (non-gzip) codec — same as .cdo.gz models;
    decompressor = libpress chain (Phase D target).
  * C.4 probed: CRS 0x18 records to field level (off/hash/a/b/c/d with
    hypotheses); course_mapinfo = 3 names + 2034B zero reservation
    (runtime-filled?); course_map binary open.
  * C.5 probed: SEQ = raw event stream, no magic (`docs/audio_notes.md`
    created); decode waits for the overlay player.
  14 tests PASS in `build/`; recomp untouched

- [2026-09-08] Phase C.1 VOL repack (`gt2_vol_pack` + `gt2_vol_open_mem` +
  `tools/vol_pack.py`, `test_vol_pack`): zero-rep blob byte-identical to
  the 488241152-B extent; same-size replace touches only the target span;
  grow (+1000) + shrink (half) keep neighbors/tree/span/files-0/1-post-array
  intact with a monotonic table; indices 0/1/degenerate/dupes rejected.
  Format findings: tbl unaligned (tight packing, no padding); single
  non-monotonic entry (final marker 0 → file 11580 degenerate, last valid
  11579); 5 empty files 11559..11563 (pure-growth slots, +3000 B verified);
  files 0/1 are table carriers (never replaced); the `[0xB488,0xB508)`
  "second copy" scare was the END OF THE ARRAY itself (no mirroring
  needed — caught by the tool disagreeing, corrected before shipping).
  Cross-implementation proof: C and Python zero-rep blobs hash-identical
  (sha256 9630aad...). 13 tests PASS in `build/`; recomp untouched

- [2026-09-08] Phase B CLOSED. Recomp ceiling exhausted, all safe items
  shipped or decided:
  * Widescreen VERIFIED in-race: `gpu_state active=1 x_margin=53
    squash=[3,4]`, `docs/screenshots/demo-race-16x9-chase.png` (1280x720,
    Lap 2/2 2nd) fills 16:9 edge-to-edge — no cutoff/void/pop-in, HUD
    native-proportion. Scissor fix holds. No `cull.keep` needed.
  * Tags: 0 callbacks in 300s; funnels absent static+interp; retag of hot
    PCs rejected by shape (no anchor, `$a0` reused). Inert-but-harmless;
    world-correct without them. Player-drive paths still unprobed (nav).
  * 60fps guest VERIFIED (256/256 unique wr_hash; pollhack 0) — no
    interpolation needed. Internal scale 2x confirmed on NVIDIA; seams
    clean at 2x bilinear (4x open).
  * PGXP SAFE but unquantified → default-off (flawless dirt reel, no VRAM
    effect by construction, same-frame GL A/B deferred with protocol).
  * Fast-loading NO effect (clean 85s vs 85s boot→f6000, plugin engaged
    per log) → default-off. `disc_speed` stays off (timing risk).
  * Cheats: pipeline ready, no addresses → deferred to C/D. Controller
    digital stands. Bezel deferred (margins already filled).
  * Reel mapped: Shelby tarmac (f6k-11k+) → … → Subaru dirt (~22k) →
    title (~26k, Start/Replay Theater). Deterministic per-frame
    (byte-identical across runs) — A/B foundation.
  * Hygiene: fresh `build/` binary; `build-debug/mods/state.toml` now
    enables widescreen (matches release; was empty — all prior debug runs
    were 4:3, fps verdict unaffected). Xvfb stopped; stray procs reaped.
  Recomp untouched (config/docs only)

- [2026-09-08] Phase A CLOSED (demo path proven; nav/save blocked on menu-flow
  RE): nav v6 full run (fresh binary, 420s, frame 55500, 64 dumps, only 5
  unique — all GARAGE hub, cursor parks on EXIT/trophy hotspots proving input
  works, but hub→race-entry never engages; phases reached drive f24800 yet
  frame 43500/48300/55500 still show GARAGE). Menu-scene hot set differs
  (`0x8001F2C8` 869k, `0x800215C8`/`0x8001BA20` 465k/463k; parent/funcB 0 in
  menus — cold finding reconfirmed). `gt2_miss 0x800177Cx` fixed set called
  but unserved. A.1-nav + A.3-save (no save trigger without a race) move to
  Phase F menu/UI flow — not more harness tweaking. Demo path (A.1-demo,
  A.2, A.4) stands as the in-race harness. Recomp untouched
- [2026-09-08] Phase B.1 funnel verdict (build-debug TCP 4372, demo drive
  f9330/22504): 4 registered widescreen funnel PCs (`0x8001C17C/0x800234F8/
  0x80019B58/0x8002106C`) execute NOWHERE — absent from 570-entry
  `dirty_ram_stats` interp table (4.9M blocks) AND from `[gt2_phot]` static
  histogram AND zero `[GT2_WS]` callbacks in 300s (squash engaged, 16:9
  mod active). Interp top = `0x00002954` 282k + `0x000177Ax` 30k sets
  (latter = the `gt2_miss` unserved set). Conclusion: registered funnels
  are not the demo-reel render path; retag candidates are the actual hot
  per-object PCs (`func_80027BBC/80028394`, anchor semantics TBD) — needs
  `$a0`/SXY-store RE before registering. `ws_census` on/off toggles OK,
  range dump returned 0 rows (ring/frame-range semantics TBD). Recomp
  untouched
- [2026-09-08] Phase A.4 GUEST-FPS VERDICT (build-debug TCP 4371, demo drive):
  `frame_fingerprint count 256 frame_lo 7300` → 256/256 unique `wr_hash`,
  0 consecutive repeats (run-length all 1s); `vblank_rate` → paced raise
  7372 / delivered 7199 / pollhack 0. Guest renders NEW content every
  vblank (60fps guest in 60fps present, not 30-doubled); no poll-fallback
  bug. `frame_interpolation` NOT needed for content rate (optional only
  for >60Hz panels). Recomp untouched
- [2026-09-08] Phase A.2 in-race hot-set named: `func_800279E8` (468B/15blk,
  mfc2×4/mtc2×8 GTE-setup helper, 96k hits), `func_80027BBC` (96B/2blk +
  CPS resume `80027C08`, per-object tiny helper, 233k — hottest in race),
  `func_80028394` (220B/13blk + 12 resume points, per-object worker,
  113k). Phot PCs are CPS continuations, not fresh entries — same hazard
  class as parked P2 (no return-1 hijack). All member 74BDB962. Recomp
  untouched
- [2026-09-08] Phase A.1 FIRST REAL RACE (via attract demo): `GT2_AUTO_DEMO=1`
  300s headless run reached frame 11648 with 19 progressing VRAM dumps
  (all distinct md5s, 320x240): Replay Shelby GT350 '66, Lap 1/2, 5th→4th→2nd
  with advancing times — first real in-race 3D rendering. In-race profile:
  parent 3583 + funcB-entry 3584 calls (~0.3/frame, ~17 objs/call, 61k objs),
  new hot set in member 74BDB962 (`0x80027BBC/0x80027C08` 233k,
  `0x800283DC/0x8002841C` 117k/116k, `0x80028394` 113k, `0x800279E8` 96k;
  small 96-468B per-object helpers, NOT the menu-hot `0x800163C8` set);
  static 17.3M hits; gate bypass 37k + class 19k, reject 0, accept 0;
  near=0 far=0 (objects gated before LOD — anchor placement verified
  correct in Size-3368 funcB). Screenshots:
  `docs/screenshots/demo-race-attract-4th.png` (f8700),
  `docs/screenshots/demo-race-attract-2nd.png` (f11400). Recomp untouched

- [2026-09-08] Weight-table writer FOUND + true-weight chain: b00 sanitizer builds weights from the SCUS charset (`-0..z`, uppercase fold, 62 slots; rule reproduces emulation byte-exact) — ported as `gt2_weight_init`. Native boot now runs true weights: sorted car table, 1336 logo stores, z149=27, matching `tools/boot_chain.py` BOOTSTATE exactly. Also fixed a harness shadow-map bug (bogus 0x801D segment swallowed reads; all port vectors re-verified clean). 12 tests PASS; recomp untouched

- [2026-09-08] Native task0b runner (`gt2_task_boot_run`): composes cache → b0 → b1 → b3 → b4 → b5 → b6 → b7 in game order on native buffers (b2 skipped, HW); end state verified (1110 cars, 126 CRS, span 0, all patterns). 12 tests PASS in `build/`; recomp untouched

- [2026-09-08] Q2 index cache (`gt2_q2_cache_build`): 0x80010228 emulated whole (413240 steps) — 248 paths, 232 pinned incl. 4 dir paths (first-file idx), 16 regional 0xFFFF; SCUS consumer scan (sole readers: asset family + span). New `tools/q2_cache.py`. 12 tests PASS in `build/`; recomp untouched

- [2026-09-08] Carlogo annotate (`gt2_car_logo_annotate`): 0x80011820 emulated whole (1673 pairs, 1361 lookups, 536 stores incl. 104 overwrites, 690 backfills to 149) — annotates the carobj `z` field, no separate table (old "hash table" note corrected); name[5] p/q filter; weights injected (writer still open: no `0x801EF630` ref in SCUS/overlays). New `tools/car_logo.py`. 12 tests PASS in `build/`; recomp untouched

- [2026-09-08] Task tails (`gt2_task`): b3 emulated whole (13115 steps, 0xAA-prefill) — 5 phases at `0x801C98E0` (0xB6 + 2×0x52 gather with file-backed 0x800A page, byte marks + 2 far inits, 126 slot inits, 6×10 block inits, 3 list + 1 big init); b4/b5/b6 proven (72/1/12 B). Two first-draft bugs caught by emulation (b6 over-clear, 0x800A shadowing). b2 corrected to descriptor-store + HW CD-kick (docs-only, not portable). New `tools/task_tail.py`, `docs/task_notes.md`. 12 tests PASS in `build/`; recomp untouched

- [2026-09-08] Asset batch loader (`gt2_asset`): task0b1 emulated end-to-end with real CD bytes (28088 steps, no traps) — `/crsmap` stem-hash index (120 entries, first tbl 8156, rol6 hash `0x80083004`), sector-window load of cache slot 6 → tbl 8 `/.crsinfo` (0xFC5 B at VOL+0x9C000, CRS header at window+0), 126-record parse/relocate with all targets in-window and rec0.hash == crsmap[0] (`Autumn Ring…`). Port keeps offsets (= rebased − base, proven equal), linear find (tables unsorted). New `gt2_vol_pread`, `tools/crs_asset.py`, `docs/asset_notes.md`; corrected "6 files" → cache slot 6. 11 tests PASS in `build/`; recomp untouched

- [2026-09-07] VOL tree-walk port (autonomous): unified entry-table model proven (11620 slots @VOL+0xB800; flat hash-dir = table tail; `date` = mastering timestamp, no name-hash exists). New `gt2_vol_stat_path/find_path/list_dir`, `gt2_cd` raw+cooked I/O, `tools/mips_emu.py` (self-tested interpreter that ran the REAL search_vol_dir: 75/75 resident Q2 paths, misses = non-resident slots explained), `tools/vol_walk.py`, `tools/xcheck_paths.py` (C==ref on all 248 Q2 paths, 0 fails). `test_vol` + `test_cd` PASS in `build/`; recomp untouched
- [2026-09-07] ISO9660 layer (autonomous): task082/task3 emulated end-to-end (ISO resolver + descent worker; task30 = strncasecmp, task083 = CD utils not a loader; records `{len@0,extent@2,size@10,namelen@32,name@33}`). New `gt2_iso` (stat/read, `;1` required, case-insensitive); `gt2_vol` rewired onto `gt2_cd` (raw dumps open directly). Q8 answered (VOL LBA from `GT2.VOL;1` root record). `test_vol` + `test_cd` + `test_iso` PASS in `build/`, xcheck still 0 fails; recomp untouched
- [2026-09-07] OVL container (autonomous): header decoded (`hdr_size` + per-member `comp_size`/`off`), all 6 members inflate byte-exact to `overlays/*.exe`. Loader emulated to the gunzip chain (mailbox `0x801C945C`, staging `0x800A8D5C` → libpress → VRAM `0x80010000` reusing boot ovr0; entry task0a `+0xC0`). New `gt2_ovl` (open/range/read_member, zlib) + `test_ovl` PASS raw+cooked in `build/`; dispatch/heap model left open (`docs/overlay_notes.md`); recomp untouched
- [2026-09-07] Overlay manager + boot map (autonomous): setjmp/longjmp mechanism proven (`0x8007AD58` save / `0x8007AD90` restore; `0x8005D9F0` checkpoint + callback-on-restore; mailbox = 12-word save area); gunzip chain IDs (`FlushCache` thunk, 1.18 MB memzero); CD-read returns member idx in a0 (stub artifact → wild stride → fault, mechanism proven); `0x80078370/83DC` = SPU (no-ops for loading). Boot sequence fully mapped (`start` → `gt2_main` → `ovr0_task0` 9-step order) in `docs/boot_notes.md` + native-boot checklist. All 4 tests + emu self-test PASS; recomp untouched
- [2026-09-07] Full-load replay (autonomous): `tools/ovl_load.py` drives `load_overlay_default` with real stubs — phase-A gunzip miss tolerated, AD90 descriptor→member mapping confirmed, phase-B inflates byte-exact (m0/m1) to VRAM `0x80010000`. Fixed emu segment shadowing (later maps win; self-test still PASS). Post-load third pass walks off-model (dispatch fill still open, bounded). All 4 tests PASS; recomp untouched
- [2026-09-07] Frontier batch (autonomous): dispatch fill CLOSED by write-watch (loader never touches `0x801EF610` → overlay self-registration); `gt2_save_crc32` ported (`0x80083178` = standard CRC32, emulated check `123456789`→`0xCBF43926`); `gt2_spu` voice table ported (`0x80078408`: 24×0x28 pattern + 0xFF unlink, byte-exact vs emulation; corrected table addr `0x801EFE68`). 6 tests PASS in `build/`; recomp untouched
- [2026-09-07] Sysinit cluster (autonomous): `gt2_sysinit` 11-step order fully mapped (memzero → ResetCallback → vsync_setup → CdInit → SPU → card/FS? → PadInitDirect → GPU → GTE → sysclock); `gt2_sysclock_combine` ported (`0x800108C0` emulated: Timer2 `0x248` + `t0^(t1<<4)^…` → `0x801C93D4`); GTE/GPU/SPU-setup ID'd as BIOS/HW-only (B(0x56), thunks, vectors — documented, not ported). 7 tests PASS in `build/`; recomp untouched
- [2026-09-07] Task registry (autonomous): task0b's 8 tasks mapped (car_loader, crsmap load+relocate, flag/struct inits, cache-count); `gt2_vol_span` ported from task0b7 (`cache[229]-cache[228]-1`, emulated incl. the boot-RAM 0xFFFF wrap + paged-in verification); asset-batch primitive `0x8005D8A0` documented for `gt2_asset`. 7 tests PASS in `build/`; recomp untouched
- [2026-09-07] Car index (autonomous): car_loader's 6 stages classified (sanitizer, no-op b01!, 4 indexers); `gt2_namehash` ported (suffix-weighted trie — caught reversed order via emulation); carobj 1110-car flat build ported + FULL 1110-entry xcheck vs emulation; bsearch port verified on synthetic table; weight-table writer + carlogo/wheel/engine shapes documented open (`docs/car_notes.md`, `tools/car_index.py`). 8 tests PASS in `build/`; recomp untouched
- [2026-09-07] Wheel/engine codecs (autonomous): `gt2_wheel_codec` ported (maker<<28|num<<16|cls<<13|name[7], signed bytes, 9-pair maker table) + stride-1 build (192 entries); `gt2_engine_parse` + stride-9 build (305 sounds, dual end); full-table xchecks 192/192 + 305/305 vs emulation. 8 tests PASS in `build/`; recomp untouched
- [2026-09-07] Task tail recon (autonomous, docs-only): weight-table writer hunt (no static writer in SCUS/overlays/VOL — narrowed to overlay-ptr/computed); task0b2 = memmove-down-16 + effect-free memset call (emulated, AA-proven); task0b3 = ~189×0x52 gather from static tables (`0x80091570`, `0x800A6ED8`, …) with byte-matched sources; task0b4 = 64 B clear + s16 bounds pair. No new port (nothing with standalone semantics — recorded honestly). 8 tests PASS; recomp untouched
- [2026-09-07] Memcard parser (autonomous): PSX 128 KB layout from PSXSPX, verified field-by-field vs `saves/card1.mcd` (fresh format: MC/0x0E, 15×0xA0, empty broken list); new `gt2_mcd` (open/dir/read/chain/checksums) + `test_mcd` with self-contained synthetic GT2 save (SC frame + CRC32 tail, round-trip incl. `gt2_crc32`). Custom `.gz`/raw-TIM detours rejected (custom formats). 9 tests PASS in `build/`; recomp untouched
- [2026-09-07] CD-init emulation (autonomous): PVD paging + `CD001` check (fixed BIOS A(0x18)=memcmp mislabeled as bzero), root-extent extraction, `;1` normalization proven, handle=open-file cursor (resolve moves paging id; verified req=331 + id-match skip), low-RAM-vector fallback edge documented honestly. No new port (`gt2_iso` already takes the robust path). 10 tests PASS in `build/`; recomp untouched
- [2026-09-07] Boot map to 100% + SPU barrier (autonomous): remaining callees ID'd (reg pokes, wrappers, HW blocks — documented, not ported); overlay entries proven non-uniform (per-function dispatch reframed); `gt2_spu_wait_idle` ported from `0x80078370` (injectable status fn + 0x675BFF timeout, tested incl. timing edge). 10 tests PASS in `build/`; recomp untouched
- [2026-09-07] mcd write path + boot orchestration (autonomous): `gt2_mcd_format` byte-identical to the game card, `write`/`delete` with chaining + checksums (multi-block, dup/nospace handling); `gt2_boot` native open→vol→ovl chain + overlay load with reference cross-check. 11 tests PASS in `build/`; recomp untouched

- [2026-09-07] Native-port scaffold (`decomp/`): clean-room GTFS/VOL reader (`gt2/vol.h`, `src/vol/vol.c`) + `tests/test_vol` (PASS: 11581 files, 11568 names, arc_topmenu/champtim sizes vs disc) + `tools/vol_dump.py` (cooked/raw-2352 list/find/extract/cook) + `decomp/docs/gtfs_notes.md` (verified spec, Q1-Q9 incl. 34 long-name records finding). Builds standalone and inside root `build/`; root configure re-verified. Recomp untouched and still the playable harness

- [2026-09-06] Menu-navigation campaign (auto-input v1-v6, screenshot-driven): title screen flawless; garage + GAME STATUS + My Home hub all render correctly. Decoded the cursor (magnetic arrow; hotspots hold, empty space snaps to center). Reached My Home hub once via EXIT; hub->arcade still open (exit->hub transition flaky ~1/5, transition-eaten taps). v6 uses holds + armored windows. Attract-demo mode added (GT2_AUTO_DEMO=1, untested). Blocker status: no run has reached a real race yet — all prior "race" windows were car-select/status screens; threading verdict is provisional pending in-race data
- [2026-09-06] Profile-first v2: static-dispatch ledger proves static backend serves ~500K overlay calls by frame 8.7K (menu phase) with loader shards at zero — but parent/funcB get NONE of them. Hit histogram (`[gt2_phot]`, `tools/patch_overlay_prof.py` hit sites) names the real menu-hot set: 0x800163C8 (72B, ~1/frame) + 0x80016410/0x800167A4/0x800167FC/0x80016980/0x800168AC, all in overlay member 4FC1D21E — parent/funcB's member 74BDB962 is simply not resident outside its scene. P2's "parent cold" finding confirmed structurally. Also: PSX_DEBUG_TOOLS=OFF in this build, so no TCP 4370 (phase_hot unreachable); env-log profiling is the path. Pending: in-race histogram (run launched) to name the race-phase hot set before picking funcB-inline vs render-thread
- [2026-09-06] P2 batch-dispatch: PARKED parent-hijack after 10+ build-run cycles. Findings: (1) arena fix real (host ptrs can't flow through guest regs — was the FEE90033 crash); (2) CPS-suspend incompatibility: parent/funcA are CPS state machines, interpreter route expects callee-suspend (CRES_NL_PC), return-1 kills boot deterministically at 9.3M insns — exonerated cycles/IRQ/scratch/GTE/RAM/mailbox one by one, noapply run proves pure control-flow kill; (3) cold-path discovery: parent emits NOTHING in-race (20K calls total=0), 4 funnel entries never fire (census: 1250 untagged polys/frame) — true hot path is funcB's inline RTPS loop. Infra kept (pool/clone/scratch-priv/detach, all env-gated + inert). Next perf step: profile-first on funcB-inline or host-side render thread
- [2026-09-06] Widescreen right-margin cutoff: GL scissor widened left-only, never right — fixed symmetric (psxrecomp f453519a), built + smoke-tested, pushed; awaiting in-race playtest confirm. Game-side outcode cull (camsetup per-object bounds) remains suspect if cutoff persists
- [2026-09-05] Fixed 18 root-owned files (src, build artifacts, scratch logs) → bob2142:bob2142
- [2026-09-05] All 6 overlays static (1305 funcs): boot interp spike gone, steady 45K insn/s = kernel/scheduler territory. Game code fully native; pushed
- [2026-09-04] Static overlay codegen unblocked: synth loop was O(n²) string rebuilds (25MB src × 10k iters); shift-tracking fix in psxrecomp/tools/compile_overlays.py, full gt2_01 static compile running
- [2026-09-03 live] Squash-mode 16:9 (native_wide=false, gte_game_mode=true): killed side-margin flicker; native-wide double-draw at 4x was the slowdown/barely-render. Texture seams at 4x still open
- [2026-09-03] Widescreen sprite-tag plugin (overlay-aware function_entry_plugin at 4 gt2_01 PCs + prologue guard, hud_sprt_squash, 16:9 active) — runtime verification pending
- [2026-09-03] Docs: fps pacer 60Hz measured, widescreen table fix, manifest 1.0.0
- [2026-09-03] README build/run docs + WIDESCREEN_RE static findings
- [2026-09-02] Enhancements batch 1: resolution/audio config + widescreen mod scaffold
- [2026-09-02] Input breakthrough (SIO auto-input), garage visual proof, save status

## Next

- Phase F.1b: name the 6 lockstep miss callees + 6 phot hitters
  (enclosing-function analysis + resident-CRC recorder upgrade);
  member residency timeline across phases.
- Then: save system (`gt2_save_frame_*` once a real save exists — still
  blocked on nav); 0x8006B61C + wrapper inflate ports (need pages/gzip).
- Carried: lazy-registration question, codec decoder entry, PGXP
  same-frame A/B, 4x seams, logo/champtim/CRS/SEQ semantics.
- Perf profile-first (VERDICT, PROVISIONAL — menu-phase data only): automation never reached a real race (all "race" windows were car-select/status screens), so the no-hotspot finding covers menus only. True in-race residency (74BDB962 scene member?) still unmeasured. Navigation to a real race is the critical path; re-run profile AFTER first real race. (Original verdict text kept below for the record: SIGPROF+ledger+BENCH converge on menus — 60fps held, wall ~ guest 25-30 / render 14-36 / pacer rest, no coarse guest hotspot, biggest item SCUS func_80094DC8 59KB/~10% wall. Infra kept env-gated + inert.)
- Perf: GT2 3D runs interpreted (5-7M interp insns/s); static overlay codegen for gt2_01 is the native-execution fix
- Texture seams at 4x bilinear (open, both 2D+3D)
- Prove end-to-end save (Sim race completion → card1.mcd GT2 blocks)

## Notes

- Uncommitted working tree: src/mods/gt2_widescreen.c, game.toml, docs/ENHANCEMENTS.md, docs/WIDESCREEN_RE.md, manifest.toml, psxrecomp (M runtime/src/main.cpp headless patch)
- GT2_01 is 3D render overlay; no screen-cull funnel found (GPU auto-clip assumed); world-space cull TBD if pop-in
- Anchor semantics UNVERIFIED (may be pointer not SXY in 2/4 funcs)
