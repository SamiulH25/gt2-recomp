# GT2 Hybrid Recompilation

Gran Turismo 2 (USA) Simulation Mode v1.2 - PSX -> Native PC (Hybrid Static Recomp + Incremental Decomp)

## Status: Phase 1 - Extraction & Analysis

### Dump Layout
- `bios/SCPH1001.BIN` (512K) - not shipped, use OpenBIOS HLE
- `SCUS_944.88` (628K) PS-X EXE `PC=8005D600 RAM=80010000 size=99000` - main executable
- `GT2.OVL` (289K -> 1.1M gzip) - compressed overlay, MIPS `addiu sp,-0x20` prologue, load addr TBD (~0x80060000?)
- `GT2.VOL` (488M) GTFS filesystem - custom Polyphony archive, 0x2FC header + directory at 0x2FC..0xBE7C

### Hybrid Approach
- **Static recomp** for bulk code (auto MIPS->C, ~60 syscalls, 1239 GTE ops) with runtime HLE
- **Manual decomp** for hot paths: GTFS loader, GPU/GTE render, physics - for enhancements (higher res, 60fps, mods)

### Quick start
```bash
python3 tools/extract.py        # bin 2352->2048 + extract + decompress
python3 tools/gtfs_parser.py    # parse VOL index
python3 tools/disasm.py         # capstone check
```

### Next steps
1. Resolve OVL load address via SCUS loader analysis (see `docs/OVL.md`)
2. Ghidra project: MIPS R3000LE, RAM 0x80010000, overlay 0x80180000
3. Recomp toolchain: `psx-recomp` / N64Recomp fork -> `src/recomp/`
4. Runtime HLE: `runtime/gpu`, `runtime/gte`, `runtime/gtfs`, `runtime/bios`
5. Pick first manual decomp target: GTFS reader (easiest win, enables asset mods)

See `docs/ARCHITECTURE.md`
