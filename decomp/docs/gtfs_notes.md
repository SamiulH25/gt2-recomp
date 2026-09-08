# GTFS / GT2.VOL notes (native-port research log)

All values measured from the US 1.2 sim disc unless noted. Game addresses
are SCUS_944.88 VRAM (`load 0x80010000`). Disassembly quoted as behavior
evidence; the port in `src/vol/vol.c` + `src/cd/cd.c` is written from the
format, not from the assembly.

## Verified

- VOL lives at LBA **473** (`473*2048 = 0xEC800` in a cooked ISO).
- Header: `GTFS` magic, `u16 data_count = 11581 @0x08`,
  `u16 entry_count = 11620 @0x0A`, reserved 0 @0x0C.
- `tbl[data_count+1]` u32 byte-offsets @0x10. File `i` at
  `VOL_BASE + tbl[i]`, size `tbl[i+1]-tbl[i]`. `tbl[0] = 0x2FC`.
- **Unified entry table** (Q1 solved): `entry_count` 32-byte slots at
  `VOL+0xB800`: `{ u32 date (mastering unix timestamp); u16 next;
  u8 flags (bit0 = dir, bit7 = end of listing); char name[25+NUL] }`.
  The "flat hash-dir at 0xBE7C" is the tail of this same table (slots
  52+, where the record framing `{zero,hash,idx,pad,name}` lands on the
  slot fields shifted by 4 — identical `(idx, name)` thanks to NUL
  padding). Root listing at slots 0..27 (28 entries: 12 dot-files, 15
  dirs, `crstim.arc`), `.text` at 28..29, `arcade` at 30..96, …,
  `sound` ends the table at slot 11619; slots 11620+ are zero.
  A directory listing is a start slot + linear scan to the END flag;
  listings overlap in scan range (e.g. `/arcade/champtim.tim` matches
  slot 54, also reachable flat). File `next` = tbl index, always.
- Init (0x800102DC) semantics confirmed by emulation: `cd_read` 0x8C000
  bytes to `vol_buffer`, header copy to 0x801E35F0, slide of the 0xC000
  window at +0xB800 over the buffer, then cache build. Resident window
  after boot = slots 0..1215 — deeper listings (carparam, dirt, engine
  sub, font, gtmenu, license, replay, sound) are paged in later by the
  loader tasks (Q6). The port reads from disc, so everything resolves.
- `search_vol_dir` (0x800100E4) fully emulated (`tools/mips_emu.py` +
  `tools/vol_walk.py`): 75/75 RAM-resident Q2 paths resolve; the 173
  misses are exactly the non-resident slots (no divergence). Match
  returns via the `beqz` delay slot (`move v0, s1`); mismatch+END
  returns nonzero; the caller treats any nonzero as an entry (reads
  low-RAM on true miss — faithfully NOT reproduced; the port returns
  NOT_FOUND instead). Callee facts: 0x80011B3C = strchr, 0x8008CF00 =
  strcmp (0 on equal), 0x8008CF64 = strncpy via strlen + BIOS `bcopy`
  (A-table 0x27, args src=a0/dst=a1/len=a2), delay-slot leading-`/`
  consumption. The 16 Q2 paths that resolve NOWHERE (regional
  `fra/ger/ita/spa arcade+license`, `eng/jpn gtmenu`) are absent from
  the US disc; the game's cache stores 0xFFFF and carries on — the port
  returns NOT_FOUND for the same inputs.
- Counts reconciled (Q7): 11620 slots = entry_count exact; 11578 file
  slots vs 11581 data files (file 0 = table head, file 1 = table tail,
  one empty file at tbl[11559]==tbl[11560]); 11568 flat names = the
  slots from 52 on that the old tooling framed as records.
- `date` is a real timestamp (Jul–Dec 1999), not a hash (Q5 solved —
  there is no name-hash algorithm; `course_map`/`course_mapinfo` share
  a batch date). The 34 nonzero-first-word records (Q9) are names past
  20 chars spilling `z`/`gz` into the framing word; idx valid.
- Raw-2352 and cooked reads agree byte-for-byte (`test_cd`).

## Open questions (next RE steps, CLI-friendly)

- **Q2 index cache.** `0x80010228` pins the 248 Q2 paths (SCUS
  `0x8009118C`, strings at `0x8008E024+`) into u16 cache at RAM
  `0x801E2EF0` (0xFFFF = missing). CLOSED: emulated whole (413240
  steps, 232 pinned / 16 miss; dirs pin first-file idx), ported as
  `gt2_q2_cache_build`. SCUS readers: the `0x8005D8A0` asset family +
  `0x8001047C` span (slots 228/229). See `docs/asset_notes.md`.
- **Q4 Sector I/O.** Done in port (`gt2_cd` + `gt2_vol` on top, raw and
  cooked). Game side (`0x8005D74C` sector math, `0x8005D7D0` LBA base at
  `0x801Dxxxx-0x6C18`, low-level `0x8007AB78`/`0x8007AD20`) mapped but
  not emulated; remaining only if cycle-accuracy is ever needed.
- **Q6 File loaders.** Solved by emulation: task082 (`0x80011154`) =
  ISO9660 resolver (handle in a0, path in a1; status byte at +0x44,
  0 = hit, 3 = done/miss; returns dir-record pointer into the paged
  window); task3 (`0x8001124C`) = descent worker (pages 0x800-byte
  windows via `0x80010F1C` → `0x8007AB74(dst, lba, 2048)` =
  CdReadSector, scans `{u8 reclen@0; extent@2; size@10; namelen@32;
  name@33}` with strncasecmp matcher task30 `0x8008D060`, builds paths
  with strcat `0x8008D020`); task083 (`0x80080EEC`) = CD-position/table
  utils, not a loader (12-byte-struct table at `0x80095ABC`,
  sector math at `0x80080F24`). Ported as `gt2_iso` (flat root only —
  the disc root has 5 entries). Open detail: multi-sector directory
  paging + the 0x80011390 wrapper's callers.
- **Q8 VOL discovery.** Answered: LBA 473 + size 488241152 come from the
  ISO9660 root record `GT2.VOL;1` (PVD sector 16, root extent 22), not
  hardcoded. The game reads it through the Q6 path.

## Tooling

- `tools/vol_dump.py` — list/find/extract/tbl/walk/cook from cooked ISO
  **or** raw 2352 `.bin` (auto-detected).
- `tools/mips_emu.py` — minimal MIPS-I interpreter (capstone decode,
  delay slots, lwl/swr family, BIOS A-table hooks) + self-test.
- `tools/vol_walk.py` — RAM-model harness: replicates init's memory
  effects, emulates the real vol functions (single path, all Q2, cache).
- `tools/xcheck_paths.py` — C port vs Python reference on all 248 Q2
  paths (232 resolve incl. ranges, 16 agree NOT_FOUND) + slot sweep.
- `decomp/tests/test_vol.c`, `test_cd.c` — regression gates; update the
  expected counts here if another disc revision is ever targeted.
