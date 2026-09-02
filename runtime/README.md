# Runtime HLE stubs

Implement in order:
1. `gtfs.c` - host FS mapping for GT2.VOL (see tools/extract.py)
2. `bios.c` - HLE for SYSTEM.CNF TCB/EVENT, memcpy, bzero
3. `gte.c` - COP2 translation (or scalar C fallback)
4. `gpu.c` - LIBGPU -> bgfx/sokol
5. `overlay.c` - load OVL decompressed at resolved VA

Stub files to create: `gte.h`, `gpu.h`, `gtfs.h`, `bios.h`
