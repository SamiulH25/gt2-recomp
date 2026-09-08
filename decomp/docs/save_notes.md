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
- Write path: format + allocate + write + checksums (`gt2_mcd`
  currently parses/reads only).
- Save state machine entry: SCUS `0x80073978` (400+ insns, dispatches
  on card status bits through `0x8006D400`/`0x8006ACxx`/`0x8007DAxx`
  and friends). Card-response-coupled — mapped, not ported; the frame
  layout falls out once a real save exists to emulate against.
