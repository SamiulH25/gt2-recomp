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
