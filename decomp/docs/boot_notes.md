# Boot sequence (static recon + targeted emulation)

Game addresses are SCUS_944.88 VRAM. Roles from disassembly + upstream
symbol leads (verified where noted, `?` where inferred).

## Entry

- `start` (`0x8005D600`): saves a0/a1/s0–s7/gp/sp/fp/ra to the save area
  at `0x8009113C`, switches SP to `*(0x800B8D...-0x734C)` if nonzero,
  zeroes BSS `0x801C93B0..0x801FD60`, calls `gt2_init` (`0x8008CE08`),
  then `gt2_main(saved_a0, saved_a1)`, then `gt2_exit` (`0x80085974`)
  with main's return value.
- `gt2_init` (`0x8008CE08`): role open (cache/CPU setup presumed).

## gt2_main (0x8005D6E0)

Four calls, then returns 0:
1. `0x80085A98` (bool_flip) — trivial flag init.
2. `gt2_ovr0_task0` (`0x80010E14`) — the whole boot (below).
3. `0x8005D9F0` — register checkpoint (setjmp; see below).
4. `gt2_load_overlay_default(1)` — boot overlay (gt2_01).

## ovr0_task0 boot order (0x80010E14, in order)

| # | callee | role |
|---|---|---|
| 1 | `0x8007D09C` | dma/vsync setup family |
| 2 | `0x80079DBC`/`9E2C`/`9E64(0x3C)`/`9EEC`/`9EF8(4)`/`0x8007A0E0(1)` | SPU voice setup |
| 3 | `0x800686C8`, `0x8007C43C`, `0x8007F924`, `0x8007E8A0`, `0x800804F8` | ? (GPU/GTE/CD sub-inits — map next) |
| 4 | `0x80010CEC` | ovr0 sub-task ? |
| 5 | `0x80011494` (arg `0x80011D38`) | CD/ISO init (`gt2.vol` string area) |
| 6 | `0x800102DC` | VOL init (emulated: 0x8C000 load + header copy + slide + cache) |
| 7 | `0x800100C0` | task0a overlay-entrypoint stub |
| 8 | `0x80010868` | task0b (scheduler? runs the loaded overlay?) |
| 9 | `0x8007A104(0xFFF)` | ? tail init |

## Overlay context switching (emulation-corrected)

- `0x8007AD58` = saveregisters (ra/sp/fp/s0–s7/gp → struct at a0,
  returns 0); `0x8007AD90` = restoreregisters (inverse, v0 = a1).
  Together they are setjmp/longjmp for overlay switches.
- `0x8005D9F0` = checkpoint: saves regs to the mailbox
  (`0x801D0000-0x6BA4` = `0x801C945C`, 12 words), returns 0. Its
  `jalr` arm fires only when resumed with v0 ≠ 0 (i.e. after a
  restore with a callback value) — it then invokes the callback with
  the mailbox words as args. (Earlier notes misread this as a
  dispatcher; the resolve-then-call shape is the same, the trigger
  is the restore path.)
- `gt2_load_overlay` spills (a1,a2,a3) + a stack word to the mailbox,
  runs the DAD8/table phase, `0x8007AD90`-family CD read, then falls
  through into DAD8 again. Phase B's stride `(x<<3)+12` uses x =
  a0-as-left-by the CD read: the real `0x8007AD90` returns the
  resolved member index in a0 (stub artifact proven: untouched a0 =
  mailbox pointer → wild stride → fault at `0x8005DB58`). So the
  mailbox protocol is descriptor-in / member-index-out (+ member
  bytes to staging).
- Gunzip chain (all `(staging 0x800A8D5C, VRAM 0x80010000)`):
  setup `0x80082FAC` → BIOS `FlushCache` A(0x44) thunk `0x8008C908`
  → memzero `0x8005D9BC` (1.18 MB at staging via memset `0x8008CE30`)
  → decompress `0x800847D0` (tasks `0x80083E00`/`0x80084364`).
  Native loader must zero the region too.
- Dispatch table at `0x801EF610` (stride `0x14`, validity word at
  +8): fill source still open — either the CD read's side effect or
  overlay self-registration. HIT path verified structurally
  (`move s1, a0` delay slot hands the entry to the gunzip call);
  MISS path re-reads + re-inflates. Next: correct `0x8007AD90` stub
  (return member idx in a0, member bytes to staging, watch for the
  table write) and re-run.

## gt2_sysinit (0x80010998, late init — full order mapped)

1. `0x8005D9BC` memzero staging region (task0)
2. `0x8008BC78` ResetCallback
3. `0x80010954` vsync_setup: installs handler `0x80010928` via
   `0x8008BD08`, spins until RAM `0x80011DF4` >= 4 (4 vsyncs)
4. `0x80089F38` CdInit
5. `0x8007F848` SPU setup (one indirect call via `0x8008BCD8` vectors)
6. `0x80086CE8` / `0x80086D78` / `0x80086DE8` card/FS init: CLOSED as
   BIOS-trampoline territory, not ported. `0x80086DE8`/`E68` and
   neighbors are `jr $t2` stubs to the A/B tables (A(`0x70`),
   A(`0xAB`), B(`0x4F`/`0x4E`/`0x4A`); `0x8008C9xx` callees are the
   same family); the sequencing around them is card-init. The actual
   save data path is the `0x80073978` state machine (large, card-
   response-coupled — see `docs/save_notes.md`), not this init family.
   No standalone semantics; docs-only.
7. `0x80087148` PadInitDirect (buffers `0x801F0C98`/`0x801F0CBA`)
8. `0x8007FE34` GPU setup (BIOS thunks + `0x80080858`/`0x800808C4` +
   indirect; HW-only, no port)
9. `0x80086658(0)` ?
10. `0x8008BB10` GTE setup: B-table call B(0x56), version
    memcmp/copy (`0x8008BC44` tables), COP0 bit `0x40000000`,
    FlushCache; HW/BIOS-only, no port
11. `0x800108C0` sysclock: Timer2 mode `0x248`, combine
    `t0^(t1<<4)^(t2<<8)^(t3<<12)` → `0x801C93D4` (ported:
    `gt2/sysclock.h`, emulated)

## Task registry (task0b 0x80010868 — 8 boot tasks, in order)| task | addr | role |
|---|---|---|
| b0 car_loader | `0x80011AF4` | 6 sub-calls (car pipeline — see `docs/car_notes.md`) |
| b1 | `0x80011C70` | resolve `/crsmap`, hash 120 stems (`0x80011B70`), load cache SLOT 6 (= tbl 8 `/.crsinfo`, one 0xFC5-B sector window) to `0x801E18E0` via `0x8005D8A0`, relocate 126 CRS records (ported: `gt2_asset`, `docs/asset_notes.md`; earlier "6 files" notes meant slot 6) |
| b2 | `0x80011CE4` | sys.ins preload: descriptor `{0x801E2CF0, 0x200, heap}` + heap bump + queued `{LBA 238855, 34600 B}` DMA (ported: `gt2_task_b2_queue/complete`, completed after b7 — async timing preserved; see `docs/task_notes.md`) |
| b3 | `0x800104A0` | 5-phase init at `0x801C98E0`: 2x0x52 gather, byte marks + 2 inits, CRS-count slot inits, 6x10 block inits, 3 list + 1 big init (ported: `gt2_task`, `docs/task_notes.md`) |
| b4 | `0x800107E8` | 0x40 clear at `0x801C98A0` + s16 bounds `[-0x40,+0x40]` (ported: `gt2_task_b4`) |
| b5 | `0x8001082C` | write `0x60` to `0x801C93C3` (ported: `gt2_task_b5`) |
| b6 | `0x8001083C` | 12-byte struct at `0x801EF5F0` (+0xFFFF at +0xC, holes +3..+7 kept; earlier "0x11 clear" was wrong — see `docs/task_notes.md`; ported: `gt2_task_b6`) |
| b7 | `0x8001047C` | count = cache[229]-cache[228]-1 → `0x801C93C4` (ported: `gt2_vol_span`; emulated; boot-RAM reads 0xFFFF/0xFFFF → wraps, real count needs paged-in replay slots) |

Asset primitive: `0x8005D8A0(slot, dst)` = load the sector-aligned window
for the file pinned at `cache[slot]` via the `0x8005D848` window reader
(`tbl[i]&~0x7FF`, len `(tbl[i+1]&~0x7FF)-tbl[i]`) + `0x8005D7D0` sector
reader (a1 = dst). Ported as `gt2_asset` (see `docs/asset_notes.md`).

## Remaining boot callees (closed)

- `0x8007A104` (4 insns): writes a0/a1 halfwords to `0x1F801D84/86`
  (called once with 0xFFF/0xFFF) — SPU/DP register poke, no port.
- `0x80085A98` (11 insns): single call wrapper (bool_flip path).
- `0x80086658` (12 insns): ResetCallback + `0x80086918`.
- `0x8007E8A0` (78 insns): memset + 4 BIOS thunks + `0x80083030` —
  HW block init, no port.
- `0x800804F8`: single BIOS thunk call. `0x8007C43C`: single memset.
- `0x80010CEC`: 4 sub-calls incl. GPU `0x8007FF70`.
- Overlay entries are NOT uniform: gt2_01/03/04/05/06 start with init
  prologues, gt2_02 starts with a clamp helper. Dispatch into
  overlays is per-function (the `0x801EF610` table), not per-member
  entry — native execution stays behind the recomp boundary (see
  `overlay_notes.md`).

## CD init (0x80011494 — emulated; Q8 mechanism + one open edge)

- Pages PVD (id 16), verifies `CD001` (via fixed memcmp BIOS A(0x18)
  — corrected: NOT bzero; the old stub mislabeled it), extracts root
  extent into the handle, resolves `gt2.ovl` (wrapper `0x80011390`
  copies the 0x38-B record to handle+0x22).
- `;1` normalization proven: `gt2.vol` → `gt2.vol;1` matches
  `GT2.VOL;1` case-insensitively (len 9); root also holds
  `MUSIC.DAT;1`, `STREAM.DAT;1`, … past the 5 known records.
- Handle = open-file cursor: resolving moves it (record extent
  becomes the next paging id — verified: paging#3 req=331 after the
  OVL resolve, #4 skips on id match). Consequence: the direct
  `gt2.vol`/`music.dat`/`stream.dat` resolves page OVL sectors,
  miss, and continue through a low-RAM vector read (`lwl` at 0)
  into the `0x800959C0`-default fallback (`0x80080EEC` setter).
  With zeroed vectors the model boots to fallback and returns
  (LBA cell reads 0 — real BIOS vectors decide the hardware value).
- Port impact: none — `gt2_iso` resolves from the PVD root directly,
  the robust path the game itself uses first.

## Toward a native boot (checklist)

Port order: PVD/ISO (`gt2_iso` ✅) → VOL (`gt2_vol` ✅) → OVL bytes
(`gt2_ovl` ✅) → dispatch-table fill (overlay self-registration) →
SPU voices (`gt2_spu` ✅) → sysclock (`gt2_sysclock.h` ✅) →
vsync/pad/GPU/GTE HW backends (host SDL: `gt2_host` — vsync counter +
timer ticks ported, pad/card/GPU/GTE documented gaps) →
scheduler/task0b (b2 queued/completed ✅) → overlay execution
(recomp today).
