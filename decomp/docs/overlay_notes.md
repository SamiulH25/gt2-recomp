# GT2.OVL / overlay loader notes (native-port research log)

Game addresses are SCUS_944.88 VRAM. Loader behavior confirmed by
emulation (`tools/mips_emu.py` + BIOS/CD stubs); the port
(`src/ovl/ovl.c`) covers the container, not the RAM-side dispatch.

## Container (verified, all 6 members byte-exact vs overlays/*.exe)

- ISO `GT2.OVL;1`, extent 331, 289752 bytes.
- Header: `u32 hdr_size = 0x30`, then per member `u32 comp_size[i]`,
  with member offsets `u32 off[i]` for i in 1..5 (member 0 starts at
  `hdr_size`, member 5 runs to EOF). Each member is one raw gzip stream.
- Members (comp → decomp): 144709→316920, 44333→248004, 53389→275780,
  5195→11500, 38461→273012, 3602→8416. Total 1133632.

## Loader (emulated; corrected — see boot_notes.md for the mechanism)

- Entry: `gt2_load_overlay_default(idx)` (`0x8005DA3C`) reads a pointer
  from the table at `0x80091174 + idx*4` and calls `gt2_load_overlay`
  (`0x8005DA7C`, args in a1/a2/a3). Table entries are mixed code/data
  (e.g. idx1 → `0x80011384`, a code trampoline): loader
  descriptors/callbacks, not names.
- `gt2_load_overlay` spills args to the register save area at
  `0x801C945C` (`0x801D0000-0x6BA4`, 12 words — a setjmp buffer, NOT a
  descriptor mailbox), runs the DAD8/table phase, the `0x8007AD90`
  CD read, then falls through into DAD8 again. The CD read resolves
  descriptor → member index, returned in a0: phase B's stride
  `(x<<3)+12` needs x = member index (stub left a0 = save-area pointer
  → wild stride → fault at `0x8005DB58`, mechanism proven).
- Staging buffer `0x800A8D5C` receives member bytes (sector reads via
  `0x8007AB74(dst, lba, len)` with the LBA base from RAM globals), then
  the libpress chain runs with dst VRAM `0x80010000`: setup
  `0x80082FAC(src, dst)` → BIOS `FlushCache` A(0x44) thunk `0x8008C908`
  → memzero `0x8005D9BC` (1.18 MB at staging via memset `0x8008CE30`)
  → decompress `0x800847D0` (tasks `0x80083E00`/`0x80084364`).
- Load base is shared: every member targets `0x80010000`, reusing the
  boot ovr0 region (`0x80010000..~0x8004D6E0`; gt2_01's 316920 bytes
  fit exactly). Overlay entry is the task0a stub (`0x800100C0`),
  which calls `0x80010000` (the member's init) with `0x801FF610`.
- Context switching is setjmp/longjmp: `0x8007AD58` saves regs,
  `0x8007AD90` restores them (v0 = a1); `0x8005D9F0` checkpoints, and
  its `jalr` arm fires when resumed with a callback value. Overlay
  exit = restore SCUS context.
- Dispatch table at `0x801EF610` (stride `0x14`, validity at +8):
  CLOSED (write-watch experiment): zero writes touch the region during
  the entire emulated load (real sectors, real member bytes, real
  inflate) — the loader never fills it. Fill = overlay
  self-registration after entry (recomp/execution territory, not
  loading). Full-load replay (`tools/ovl_load.py`): phase-A gunzip
  failure on empty staging tolerated (v0=-1); AD90 serves member
  bytes; phase-B inflates byte-exact (m0 316920 B, m1 248004 B) to
  VRAM `0x80010000`; the post-load pass is overlay-registered
  dispatch, out of loader scope.

## Open (toward boot emulation)

- Dispatch-table fill (`0x801EF610`): CD-read side effect or overlay
  self-registration — one correcting-stub experiment away.
- Per-member entrypoints: task0a (`+0xC0`) holds for the boot member;
  other members start with different prologues (gt2_02 begins
  `2a10a400`) — entry per member TBD by overlay RE.
- `0x80078370`/`0x800783DC` = SPU voice poll/init (scratchpad
  `0x1F801C00` table, structs at `0x801FF478`) — no-ops for loading.
- Port: `gt2_ovl` exposes bytes + `GT2_OVL_VRAM_BASE`/`ENTRY_OFF`;
  actual member execution stays in the recomp/runtime until a native
  boot exists (checklist in `boot_notes.md`).

## Tooling

- `decomp/tests/test_ovl.c` — header + inflate vs `overlays/*.exe`
  (raw + cooked). `tools/vol_dump.py extract` handles VOL files only;
  OVL members come from `gt2_ovl_read_member` / `tools/split_ovl.py`.
- `tools/ovl_load.py` — replayable full-load emulation (idx0/idx1
  byte-exact to VRAM); ends at the open post-load pass.
- `tools/ovl_pack.py` — repack: parse header, rebuild (original members
  → byte-identical round trip, verified), `--replace-member I=rawfile`
  (re-gzip), `--verify` (reparse + inflate-compare). Layout finding:
  members are followed by zero padding to 4-byte alignment (1-3 B).
  Member sizes match `docs/OVERLAYS.md` exactly
  (144709→316920 … 3602→8416).

## Member census (Phase D.1, 2026-09-09 — `tools/ovl_census.py`)

Static per-member census (VRAM `0x80010000` base) + role leads from
`_upstream/gt2-reversing` splat yamls/symbol files (leads only, and note
upstream mixes US/EU revisions — verify before trusting an address):

| member | size | COP2/RTPS/RTPT | syscall | JAL | anchors | role hypothesis |
|---|---|---|---|---|---|---|
| gt2_01 | 316920 | 1314/66/26 | 13 | 3193 | 4 | 3D render + menu (`load_global_menu_overlay`, memset_u8/u32/u16 in ovr1 syms; the only member with projection sites) |
| gt2_02 | 248004 | 51/0/0 | 30 | 1207 | 0 | REPLAY (`start_replay`, task0710/0780 callers; entrypoint0 `0x80011384` = the loader-table idx1 target; DO*/DR* license/test code strings) |
| gt2_03 | 275780 | 8/0/0 | 24 | 1529 | 0 | ARCADE RACE + FMV (`arcaderace_func16`, `DecDCTReset`; most syscalls of any member after 01) |
| gt2_04 | 11500 | 0/0/0 | 0 | 171 | 0 | SHARED RACE UTILS (`memset_caller` AT entry `0x80010000`, `large_task*`, `shared_gt_race_func4`; no strings — pure code) |
| gt2_05 | 273012 | 1/0/0 | 0 | 1192 | 0 | EVENT/LICENSE/CAREER RULES (1163 syms: `load_license`, `load_event_task*`, `is_international_league`, `is_gt_world_cup`, `is_event_synthesizer`; `LIS/LIA/LIB…%02d` strings) |
| gt2_06 | 8416 | 2/0/0 | 0 | 112 | 0 | MOVIE player (1145 syms: `fill_memory` at entry, `dctout_callback`, `DecDCT_inout`; lone string `12psxMovieLoop`) |

Notes:

- Entries are NON-UNIFORM (kills the "task0a +0xC0 fits all" model):
  ovr2 starts with `slt` prologue + max3/min3 at base, real entrypoint0
  at `0x80011384`; ovr4's entry IS `memset_caller`; ovr5 starts with
  `load_license_task0`; ovr6 with `fill_memory`. Member init = per-member
  RE, not a shared stub.
- No member besides gt2_01 has Tomba anchor pairs (0) or RTPS/RTPT (0);
  screen-cull `slti 0x140`-class hits are 0 everywhere except 5 weak
  immediates in gt2_01 (no W+H pair funnel — GPU auto-clip stands).
- gt2_02's 51 COP2 with zero projection ops: control/status reads
  (mfc2), not 3D — consistent with replay/camera logic, not rendering.
