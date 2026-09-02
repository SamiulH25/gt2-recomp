# GT2 Architecture (PSX)

## Memory Map
- `80010000-80099000` SCUS text/data (spans 0x99000 from header)
- `8005D600` entry PC (boot handler, saves regs, switches SP if needed)
- `801FFF00` STACK from SYSTEM.CNF, `801Dxxxx` globals (seen `lw a1, -6C18`)
- HW regs `1F801000`, scratchpad `1F800000`

## Files
- SYSTEM.CNF: `BOOT=cdrom:\SCUS_944.88;1 TCB=4 EVENT=10 STACK=801fff00`
- SCUS loads strings at 80011D30 `gt2.ovl`, 80011DA4 `gt2.vol`, 80011DB0? etc.
- OVL: raw 0x30 header + gzip payload at 0x30 -> decompresses to 1.1M MIPS code (no PS-X header). Hypothesis load at 0x80060000-0x80120000, need to confirm via SCUS memcpy size 0x114C40.
- VOL: GTFS magic 4 @ 0x473*2048, header 0x2FC, dir entries 0xBB80, then data blobs. Directory entries 0x20 stride, filenames like `arc_topmenu`.

## BIOS / Libraries
- Uses Sony SDK libs (strings `Sony Computer Entertainment Inc.`)
- Syscalls: 60 `syscall` ops - likely `EnterCriticalSection` etc.
- GTE: 1239 COP2 ops - 3D transforms, need HLE or software impl
- LIBGPU/LIBGTE implied, LIBCD for streaming

## Recomp Runtime Needs
- `runtime/bios.c` - threads, events, memory card, pad
- `runtime/gte.c` - COP2 emulation or translate to SSE
- `runtime/gpu.c` - PSX GPU -> Vulkan (intercepts LIBGPU calls)
- `runtime/cd.c` + `runtime/gtfs.c` - abstract CD reads to host files (VOL mapped)
- `runtime/overlay.c` - load/unload OVL at runtime (reloc handling)

## Manual Decomp Priority
1. GTFS (`0x8001146C` fopen path) - isolate, reimplement as host FS for modding
2. Overlay loader
3. Render loop (GTE heavy)
4. Physics/car data (in VOL)
