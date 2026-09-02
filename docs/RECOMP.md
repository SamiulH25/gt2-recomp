# Recomp status

Codegen succeeded via psxrecomp:

- `game.toml` probed from disc (SCUS-94488, 0x80010000, entry 0x8005D600)
- `seeds/ghidra_funcs.txt` 1179 entries (666 JAL + 1143 gt2-reversing mainexe symbols merged)
- `generated/` 358 shards, 51M lines, 1593 funcs, 31535 blocks, 2033 loops from SCUS_944.88 (612KB)
- Dispatch table 3863 entries, decls header

Next: overlay codegen for GT2.OVL (6 members, 1.1M) and runtime integration via psxrecomp/runtime.cmake

Run:
```
cmake -S psxrecomp/recompiler -B /tmp/psxrecomp_build -DPSXRECOMP_ENABLE_CHD=OFF && cmake --build /tmp/psxrecomp_build
/tmp/psxrecomp_build/psxrecomp-game --config game.toml
```
