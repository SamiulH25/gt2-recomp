# GTFS (Polyphony)

Offset in ISO: `LBA 473 * 2048 = BASE`

- `0x00 4` magic `GTFS`
- `0x10` array<u32> start offsets (little endian), monotonic increasing
  - file `i` at `BASE + tbl[i]`, size `tbl[i+1]-tbl[i]` (last file to VOL end)
  - at least 2048 entries, first is `0x2FC`
- `0x2FC` not used? Actually tbl[0]=0x2FC is start of raw offset table region? No, tbl[0] itself is 0x2FC
- `BASE+0xBE7C` directory: 32-byte fixed records (11568 entries)
  - `0x00 I` 0
  - `0x04 I` hash (maybe Jenkins)
  - `0x08 H` tbl idx
  - `0x0A B` pad 0
  - `0x0B 21` filename (null-terminated, max 20, padded to 32)
- Example: `arc_topmenu` idx 36 -> `tbl[36]=00341800` size 118784 gzip `bg01.tim`

Verified: `python3 tools/gtfs_extract.py` extracts `arc_topmenu` as gzip correctly, `champtim.tim` as 16K (format not TIM magic but valid).
