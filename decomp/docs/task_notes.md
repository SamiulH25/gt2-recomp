# Task-tail notes (native-port research log)

Task0b (`0x80010868`) runs 8 tasks; b0/b1/b7 live in `gt2_car`/`gt2_asset`/
`gt2_vol`. This covers the tails b2–b6. Port: `gt2/task.h` (helpers +
per-task entries); tests in `decomp/tests/test_task.c`; replayable
emulation in `tools/task_tail.py` (0xAA-prefilled RAM, file-backed 0x800A
page, preloaded CRS window).

## Round-trip evidence

| task | addr | emu | writes |
|---|---|---|---|
| b3 | `0x800104A0` | ok, 13115 steps | 10092 B / 493 regions (stack excl.) |
| b4 | `0x800107E8` | ok, 78 steps | 0x40 zeroed at `0x801C98A0` + s16 pair at `0x800A6F18` |
| b5 | `0x8001082C` | ok, 3 steps | `0x60` -> `0x801C93C3` |
| b6 | `0x8001083C` | ok, 10 steps | 12-byte struct at `0x801EF5F0` |
| b2 | `0x80011CE4` | ok + kick, 116 steps | descriptor + heap bump + queued DMA (see below) |

## b3 phases (all pure; callees take only RAM + memset `0x8008CE30`)

1. Gather: memset `0xB6` at base, then **2** records (stride `0x52` from
   +0xA) — not ~189 as an earlier note guessed; the 0x3C68-ish span is the
   whole task's footprint. Each record copies 11 B from each of
   `0x80091570/7C/88/94` plus 20 B of the u16 table `0x800A6ED8` at +0x3E
   (tails match `40 00 78 00 88 00 c0 00…`); gap +0x2C..+0x3D stays zero.
   Both records copy the SAME sources (addresses are loop-invariant).
   Emulation gotcha: the 0x800A page map must be seeded with SCUS file
   bytes or the u16 tail reads zero (shadowing bug, caught by vectors).
2. Marks: fixed bytes (`+0`=1, `+2`=0, `+3`=2, `+4`=1, `+5`=0, `+6`=2,
   `+8`=1, `+0xAE/+0xAF`=0, `+0xB1/+0xB2`=1, `+0xB3`=F0, `+0xB4`=C0,
   `+0xB5`=1) + two `0x80010798` inits at +0x3C74 / +0x7C9C (zero half,
   -1 half at +0x4018, `0x2710` at +0x4014, zero byte +0x401B).
   Phase 3's count comes from a delay-slot `lui`: after the phase-2 loop
   exits, `v0 = 0x801E0000`, so `s3 = 0x801E18E0` (the CRS window base).
3. Slots: CRS-count (126) x `0x8005DD68` at +0x218+i*0x24: five -1 words,
   low half of the last cleared (word reads `0xFFFF0000`).
4. Blocks: 6x10 `0x8005DE1C` at +0x1418+o*0x668+i*0xA4 (3 zero bytes,
   5 slot inits, zero tags at +0x68 step 0xC).
5. Tails: three `0x8005E07C` at +0x3A88/0x3B2C/0x3BD0 (0xA4 clear, eight
   -1 words from +8 stride 0x14) + `0x800107B4` at +0xB8 (0x160 clear,
   u32 1 at +0x40).

## Corrections to earlier notes

- b2 PORTED (was docs-only): `0x80078790` stores `{0x801E2CF0, 0x200,
  [0x80092E74]}` (no copy — the old "memmove-down-16" note was wrong);
  `0x800787CC` indirect-dispatches on a3 (b2 passes 1 → vector
  `0x8006830C`; note the `neg` first insn — added to `tools/mips_emu.py`,
  self-test still passes). That vector resolves Q2[247]
  (`/sound/sys.ins`, double-negated index math), computes
  `{LBA = 473+(tbl>>11), len = (tbl_next&~0x7FF)-tbl}` (238855/34600
  observed — an early analysis was off by exactly 20 sectors from bad
  mental hex; the instrumented run settled it) and queues the async DMA
  (HW leaves `0x8007CFDC/0x8007AB14/0x8007D024`, completion IRQ/polled).
  The kick tail bumps the heap `([DST+0x10]+0x1F)&-0x10` (16 on zeros;
  a `&~0x10` draft of the port got 15 — the test caught it).
  Port: `gt2_task_b2_queue/complete` (queue after b1, complete after
  b7 — the deposit range overlaps cache/header inputs b3..b7 read, so
  the async timing is load-bearing and preserved). Tool:
  `tools/b2_kick.py` (replayable harness run).
- b6 is NOT a 0x11 memset: exact 12-byte layout (holes +3..+7 untouched).
  The first port draft over-cleared; prefill emulation caught it.

## Open

- b2's deposit TIMING on hardware (which vsync/IRQs retire the queued
  DMA before the 0x80060884 SPU consumer parses the 0x200 header) and
  the exact `0x8007CFDC/0x8007AB14/0x8007D024` driver signatures. The
  native port preserves the observable order (queue after b1, complete
  after b7); cycle accuracy is future work.
- Overlay-side consumers of the b3 tables (semantics unknown — bytes
  mirrored exactly, no interpretation guessed).

## Native runner (gt2_task_boot_run)

Composes every ported step in task0b order on native buffers (b2 queued
after b1, completed after b7): VOL-init cache -> b0 -> b1 -> b3 -> b4 ->
b5 -> b6 -> b7, then the queued sys.ins deposit.
End state on US 1.2 sim (true sanitizer weights): 1110 cars (sorted,
1336 logo stores), 120 crsmap hashes, cache[6] = 8, 126 CRS records
(0xFC5 window), task_mem patterns, bounds, flag `0x60`, span 0. Tested
in `test_task.c` (boot-run section). Cross-checked against the full
emulated boot (`tools/boot_chain.py` BOOTSTATE: identical).
