# GT2 Overlays

GT2.OVL contains 6 concatenated gzip members (289689 bytes compressed -> 1,133,632 decompressed):

| idx | raw off | comp | decomp | first MIPS | vram |
|---|---|---|---|---|---|
| gt2_01 | 0x30 | 144709 | 316920 | e0ffbd27 addiu sp | 0x80010000 |
| gt2_02 | 0x23578 | 44333 | 248004 | 2a10a400 | 0x80010000 |
| gt2_03 | 0x2e2a8 | 53389 | 275780 | e8ffbd27 | 0x80010000 |
| gt2_04 | 0x3b338 | 5195 | 11500 | e8ffbd27 | 0x80010000 |
| gt2_05 | 0x3c784 | 38461 | 273012 | d0ffbd27 | 0x80010000 |
| gt2_06 | 0x45dc4 | 3602 | 8416 | e0ffbd27 | 0x80010000 |

All share VRAM 0x80010000 (overlayed, per gt2-reversing splat `gt2_01.yaml` etc).

Tools:
- `python3 tools/split_ovl.py` -> `/tmp/opencode/ovl_split/gt2_0*.exe`
- Wrapping with PS-X header (0x800 bytes, PC 0x80010000) allows `psxrecomp-game` static codegen:
  - gt2_01 wrapped -> 1039 funcs, 309KB (tested via /tmp/ovl_wrap)
  - Runtime capture (psxrecomp v4) will handle overlays dynamically via TCC cache, so wrapping is for analysis/Ghidra not required for shipping.

Ghidra: `tools/ghidra/LoadGT2Overlays.py` creates overlay blocks at 0x80010000.

Next: pre-split for analysis, but production build uses SCUS static (358 shards, 1593 funcs) + runtime overlay capture.
