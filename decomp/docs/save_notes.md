# Save data notes (native-port research log)

## CRC-32 (`gt2/save.h`)

SCUS `gt2_save_crc32` (`0x80083178`) is standard CRC-32 (poly
0xEDB88320, init/xorout 0xFFFFFFFF) over the table at `0x800A6ACC`.
Emulated check: `"123456789"` → `0xCBF43926`. (The same-named
`0x80073978` is a save state machine, not the CRC.)

## Memcard images (`gt2/mcd.h`)

PSX 128 KB layout, verified against `saves/card1.mcd` (freshly
formatted by the game: `MC`/`0x0E` header, 15× `0xA0` dir entries,
empty broken list, `FF` data blocks):
- Block 0 frame 0: `"MC"` + XOR checksum at 0x7F.
- Frames 1..15: `{u32 state; u32 size; u16 next; name[21]; 0;
  checksum}`. States: `0x51/52/53` in-use chain, `0xA0` fresh free,
  `0xA1/A2/A3` deleted.
- Frames 16..35: broken-sector list (`FFFFFFFF` = none).
- Data blocks 1..15 chained via `next` (block-1 based).
GT2 filenames: `"BA"` + `"SCUS-94488"` + 8 chars. Title frame:
`"SC"` + icon flag + Shift-JIS title + CLUT.

## Open

- GT2 save-frame layout inside data blocks (where CRC32 sits, car/
  progress encoding) — needs a real save. The repo cards are empty;
  end-to-end saving via the game is unproven (see `docs/SAVE_STATUS`).
- (CLOSED: the MCD write path — format/allocate/write/delete/checksums
  — is fully ported in `gt2/mcd.h`; the "parses/reads only" note above
  was stale.)

## Status driver (SCUS 0x80073720 — PORTED 2026-09-10)

Port: `gt2/card.h` (`gt2_card_run`); driver `tools/save_state.py`;
test `decomp/tests/test_card.c` (status sweep, regions, counter edges,
state sweep, OOB — all emu-exact).

- Entry (a0=state, a1=EV-PTR — a pointer, not flags; small ints trap):
  s3=-2; CFC4(+0x44); BE64(+0x28); counter trio +0x14 (sat 13→12,
  negatives climb except -1 which exits)/+0x16 (sat 61→0)/+0x18
  (sat 41→0); DA80(stack scratch, state[0]); ev NULL → -2.
- Event struct: status @+4, extra @+0xC, OR'd into s1 (caller's s1 is
  clobbered by the load — only the OR matters; s1-init unobservable
  natively, same as emu.call).
- Lanes: 0x500 (+0x1C countdown → op(2)+op3524 / op(0)); 0xA00
  (+0x1A==0xC → -1/0 on +0x1B<8/≥8; else 8CFC4 → memmove/table/6AD3C
  → R5-entry or 73524+R4, or op(2)); maze (0x10000 → D400 block with
  +0x1A forced 0x0C BY THE JAL DELAY SLOT — d400's return discarded;
  0x10/0x1000 countdown+min-clamp; R7 copy +0x4A→+0x1A; bit4/bit8
  countdowns; 0x101F tail → op(5)/-3). Returns -3/-2/-1/0.
- +0x24 is a data-window pointer (never compared — dropped for a native
  window; pointer-independence vector proves it). SCUS const table
  0x8009226C comes in as aux_tab (index (+0x1A<<4)+(+0x1B)).
- Delay-slot census (all verified against emu after misreading most of
  them once): sb-0x1A (D400 arg, not return), beq-v0=7 (arm-B style
  overwrite), andi-0x101F recompute (R10/R11 join), move-s3 (R11 exit
  carries -2/-1/0 correctly).
