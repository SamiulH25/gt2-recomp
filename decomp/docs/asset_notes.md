# Asset batch loading notes (native-port research log)

Task0b1 (`0x80011C70`) is the game's asset-batch primitive: index a VOL
directory by name hash, load a data file through the sector window, rebase
its pointers. Course data (`/crsmap` + `/.crsinfo`) is the proven model.
Port: `gt2/asset.h` (`gt2_crs_hash`, `gt2_crsmap_build`, `gt2_asset_window`,
`gt2_asset_window_cached`, `gt2_crsinfo_parse`, `gt2_crs_find`); tests in
`decomp/tests/test_asset.c`; replayable emulation in `tools/crs_asset.py`.

## Round-trip evidence (emulation, `tools/crs_asset.py`)

RAM model: vol_buffer `0x800A97D0` <- VOL slot window, header copy
`0x801E35F0` <- VOL head (VOL-init effects), `cache[6] = 8`, VOL LBA cell =
473, CD reads served from the real image. Real `0x80011C70` runs clean
(28088 steps, no traps):

- crsmap index: count **120** -> `0x801C93E4`, first tbl idx **8156** ->
  `0x801C93DC`, hashes at `0x801E33F0` (`hash[0] = 0xb70b31b6`).
- CD load: dst `0x801E18E0`, abs `0x188800` (= VOL+`0x9C000`), len **`0xFC5`**.
- CRS window: magic `CRS\0`, count **126** at +6, all 126 rebased targets
  inside the window, `rec0.hash == crsmap[0]`, rec0 target = `Autumn Ring…`.

## Mechanics (corrected twice against first readings)

- Hash (`0x80083004`): `v = rol(v,6) + byte` per char to NUL. Distinct from
  the car namehash (suffix-weighted trie). `hash("2p_autumn") = 0xb70b31b6`.
- Index (`0x80011B70`): resolves `/crsmap`, skips leading dir links
  (`flags&3 == 1`), then per file: strcpy, cut at the FIRST `.` (strchr),
  hash the stem, store u32. Ends at the next dir entry after the END file.
  Helpers confirmed: `0x8008CEDC` = strcpy, `0x80011B3C` = strchr.
- Window (`0x8005D848` + `0x8005D7D0`): reads at `tbl[i]&~0x7FF` with length
  `(tbl[i+1]&~0x7FF) - tbl[i]` (end aligned, start EXACT — the first port
  draft aligned both ends and overshot `0xFC5` vs `0x1000`; emulation
  settled it). Layout follows the ALIGNED base: the CRS header sits at
  window+0, `0x3B` before the exact tbl[8] start. The 0x3B tail of the
  loaded sector span is never read (len stops at `0xFC5`).
- Slot load (`0x8005D8A0`): tbl idx = u16 `cache[slot]`; task0b1 passes
  slot **6** (-> tbl 8 = `/.crsinfo`). Sole caller in SCUS — the old
  "batch-load 6 files" note meant cache SLOT 6, one file (boot_notes fixed).
- Relocate (task tail): count = u16 at window+6; each record (stride
  `0x18` from +8) rebases `u32@+0` by the load base. Record =
  `{u32 target; u32 namehash; ...}`. Port keeps offsets (== rebased minus
  base, proven equal); `gt2_crs_target` adds the host base.
- Lookup: both tables keep VOL listing order (UNSORTED), so the port scans
  linearly. The in-RAM consumer is overlay-side (no SCUS reader of
  `0x801E18E0`/`0x801E33F0` exists) — recorded open, not guessed.

## Data facts (US 1.2 sim)

- `/crsmap`: 120 files (`2p_autumn.tim.gz` … `testline.tim.gz`), tbl
  8156…8275. CRS records: 126, all unique hashes; 120 match a crsmap stem,
  6 extras (`0x4efe1c8a`, `0x454b35e9`, `0x358102ce`, `0x358112ce`,
  `0x358122ce`, `0x3584735f`) — data-rev skew, lookup still resolves them.
- Targets are course display-name strings (`Autumn Ring`, …).

## Open (not ported)

- Overlay-side CRS consumer (which overlay, linear vs cached lookup).
- Other `0x8005D848`-family users: none in SCUS beyond task0b1 (sole
  `0x8005D8A0` caller); overlay code may call the primitive directly.
- `gt2_vol_pread` (raw cooked-space read) was added for window loads.
