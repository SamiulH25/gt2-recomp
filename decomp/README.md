# GT2 native port ("decomp")

Fresh, human-readable C that builds and runs on the **host** (Linux/PC) with
a normal toolchain — no `CPUState`, no recompiled shards, no PSX SDK.

- Status: incremental. Each module is behavior-compatible with the original
  game code, verified against the real disc + the running recomp build.
- Method: black-box + disassembly reference. We read the original MIPS only
  to learn behavior, then write clean C from scratch. Nothing is copied from
  other RE projects; they are cited as leads, not sources.
- Layout:
  - `include/gt2/` — public headers, one per module (`types.h`, `vol.h`, …)
  - `src/<module>/` — implementations
  - `tests/` — host unit tests (CTest), driven by your legal disc image
  - `docs/` — per-module RE notes: verified format + open questions with the
    exact original addresses to look at next

## Build

```bash
cmake -S . -B build && cmake --build build -j$(nproc)   # from repo root
ctest --test-dir build/decomp -V                          # needs GT2_ISO, see below
```

The tests need a **cooked** (2048-byte-sector) ISO. Convert once from your
legal 2352-byte dump:

```bash
python3 tools/vol_dump.py --raw-bin "Gran Turismo 2 (USA) (Simulation Mode) (v1.2)/Gran Turismo 2 (USA) (Simulation Mode) (v1.2).bin" list --limit 5
# or cook explicitly:
python3 - <<'EOF'
from tools.vol_dump import cook
cook("Gran Turismo 2 (USA) (Simulation Mode) (v1.2)/Gran Turismo 2 (USA) (Simulation Mode) (v1.2).bin", "/tmp/opencode/gt2.iso")
EOF
GT2_ISO=/tmp/opencode/gt2.iso ctest --test-dir build/decomp -V
```

If `GT2_ISO` (default `/tmp/opencode/gt2.iso`) is missing, tests report SKIP
instead of failing, so plain builds never break.

## Modules

| Module | Header | State |
|---|---|---|
| VOL/GTFS reader | `gt2/vol.h` | ✅ flat + hierarchical paths, listings; raw/cooked images; tested, xchecked vs emulation |
| CD sector I/O | `gt2/cd.h` | ✅ raw-2352 + cooked sector/pread; tested raw-vs-cooked |
| ISO9660 root files | `gt2/iso.h` | ✅ `GT2.OVL;1`/`GT2.VOL;1` resolve+read; tested raw+cooked |
| OVL container | `gt2/ovl.h` | ✅ 6 members inflate byte-exact; tested raw+cooked |
| save CRC32 | `gt2/save.h` | ✅ matches emulated `0x80083178`; standard vectors |
| memcard images | `gt2/mcd.h` | ✅ parse/read + synthetic save round-trip; formatted-card verified |
| SPU voices | `gt2/spu.h` | ✅ 24×0x28 init + unlink + wait-idle barrier |
| sysclock combine | `gt2/sysclock.h` | ✅ emulated (`t0^(t1<<4)^…`); header-only |
| car index | `gt2/car.h` | ✅ namehash + 1110-entry build + bsearch + logo z-annotate/backfill; wheel/engine tables; full xchecks |
| asset batch | `gt2/asset.h` | ✅ crs hash + 120-entry crsmap index + Q2 cache build (248 paths) + sector-window load + 126-record CRS parse/relocate/lookup; emulated |
| task tails | `gt2/task.h` | ✅ b3 5-phase init (gather/marks/slots/blocks/tails) + b4/b5/b6 + native task0b runner (b2 skipped, HW); emulated |
| overlay manager | — | mechanics mapped (setjmp/longjmp ctx, save area, gunzip chain); dispatch fill = self-registration |
| boot/sysinit | `docs/boot_notes.md` | ✅ sequence mapped (entry→main→ovr0_task0 order); inits pending ports |

## Conventions

- C11, no dynamic deps beyond libc. Public API uses `gt2_<mod>_<verb>`.
- All disc offsets are `uint32_t` byte offsets; sector size is 2048.
- Provenance comments cite original addresses (`SCUS 0x8001xxxx`) and the
  tool that produced the evidence (`tools/vol_dump.py`, capstone dumps).
  Never paste lifted assembly as "implementation".
