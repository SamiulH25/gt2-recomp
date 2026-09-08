# Car data index notes (native-port research log)

Task0b0 car_loader (`0x80011AF4`) runs 6 stages; the 4 directory
indexers share one design, verbs differ. All confirmed by emulation
with a fully-resident slot table (`tools/car_index.py`); the boot-RAM
slide only covers slots 0..1535, so these run after paging (or with
the idealized model used here).

## Common shape

- Resolve `/<dir>` via search_vol_dir; skip leading dirs; walk with
  stride 4 slots (car files come in 4-groups: model + 3 variants).
- b00 path_sanitizer (`0x800116AC`): lowercases/strips the path buffer
  (`0x80091620` → `0x801FF610-0x9B0`); b01 (`0x80011708`) is literally
  `jr ra` — a no-op placeholder stage.

## carobj (0x80011710) — ported as gt2_car

- 1110 cars (slots 3514..7950 step 4, all `.cdo.gz`), count → RAM
  `0x801C93C8`, table `{u32 hash; u16 tbl_idx; u16 0}` at `0x801DF5D0`
  plus end sentinel (`next = last.next+4`, hash untouched).
- Name hash (0x80060924): 5-char suffix-weighted trie over the 256-B
  weight table at `0x801EF630` (runtime data, writer unknown — only
  reader found):
  `h = w[c4]|w[c3]<<6|w[c2]<<12|w[c1]<<18|w[c0]<<24`.
- Lookup (0x8005D950): binary search by hash, midpoint `(lo+hi)>>1`,
  miss → 0 (port: -1). NOTE: correct only if hashes are monotonic in
  slot order under the real weights — verify once weights are found.
- Full-table xcheck: C port == emulation on all 1110 entries.

## carlogo (0x80011820) — ported as gt2_car_logo_annotate

- 1673 logo TIMs in 2-slot steps (odd slots hashed, even skipped; end when
  the second slot carries END), first files at tbl 149, 151, ….
- Each full name hashed (no extension strip); `name[5]` of `0x70/0x71`
  (312 `…p…` names, no `q`) skips the lookup, 1361 lookups total.
- Hit (car bsearch `0x8005D950`) stores the logo tbl idx into the car
  entry's `z` halfword; 536 stores incl. 104 overwrites (identity weights).
- Misses backfilled with the first logo idx (149; 690 backfills); sentinel
  untouched. No separate table exists — the old "hash table" note was wrong.
- Weight-table hunt (0x801EF630, 256 B): still open. No `lui 0x801e` +
  `addiu 0xF630` pair and no raw `0x801EF630` word in SCUS or any overlay —
  writer is overlay pointer-arithmetic or runtime-computed. The port takes
  weights as a parameter (identity weights in tests, emulation-matched).

## carwheel / engine — ported as gt2_car

- carwheel (192 entries, u32 table at `0x801E30F0`, count at
  `0x801C93B4`): stride-1 walk; codec packs
  `maker<<28 | num<<16 | cls<<13 | name[7]` with signed-byte
  arithmetic, maker from the 9-pair table at `0x80033DD0`
  (`bb br du en fa oz ra sp yo`), class from name[6]
  (`4`→1, `5`→2, `6`→3). Wheel = maker + 3-digit number + size
  class + g/s suffix. Full 192-entry xcheck vs emulation.
- engine (305 entries, u16 table at `0x801DF340`, count at
  `0x801C93BC`): stride-9 walk over `NNNNN*.es` sounds, leading-decimal
  parse (`0x80011670`), dual end (8-ahead END flag or non-digit next).
  Full 305-entry xcheck vs emulation.
