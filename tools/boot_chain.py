#!/usr/bin/env python3
"""Full SCUS boot chain emulation (native-boot model regression gate).

Executes the REAL boot code with a modeled RAM image and CD stubs:
  VOL init (0x800102DC) -> build_cache (0x80010228) ->
  task0b (0x80010868: car_loader, crsinfo load, [b2 CD-kick skipped],
  task tails, span counter).

CD reads are served from the real image (0x8005D7D0 hook); the b2 fill
path (0x800787CC -> CD HW) is skipped via hook — its descriptor store is
the portable prefix. Identity weights stand in at 0x801EF630 (true
weights are runtime data, writer open).

Prints a BOOTSTATE summary comparable against the native
gt2_task_boot_run end state (see decomp/docs/task_notes.md).

Usage: tools/boot_chain.py [--iso PATH] [--scus PATH]
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, ".")
from tools.mips_emu import Emu, Trap
from tools.vol_walk import install_bios, STACK_TOP


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=os.environ.get("GT2_ISO", "/tmp/opencode/gt2.iso"))
    ap.add_argument("--scus", default="disc/SCUS_944.88")
    a = ap.parse_args()
    iso = open(a.iso, "rb").read()
    scus = open(a.scus, "rb").read()[0x800:]
    base = 473 * 2048

    emu = Emu(scus, vram_base=0x80010000, file_off=0x800)
    # NOTE: the VOL-init slide copies 0x8C000 B from vol_buffer+0xB800, so
    # the 0x800A window must cover 0x80140FD0; only its first 0x80800 B
    # are real VOL bytes, the tail is stale-RAM collateral (unused).
    # There is NO separate 0x801D page: 0x801Dxxxx addresses live in the
    # 0x801C page's upper half (a wider 0x801D map would shadow reads!).
    emu.map(0x800A0000, bytearray(0xA8000))
    emu.map(0x801C0000, bytearray(0x20000))
    emu.map(0x801E0000, bytearray(0x20000))
    emu.map(0x801F0000, bytearray(0x10000))
    install_bios(emu)
    # No weight pre-seed: real boot has BSS zeros here and the task0b
    # sanitizer (0x800116AC) builds the true table from the SCUS charset.
    emu.write_word(0x801C93E8, 473)   # VOL LBA cell (ISO-init effect)

    loads = []

    def cd_read(e):
        # 0x8005D7D0(dst, sector, len): serve from the image.
        dst, sec, ln = e.regs[4], e.regs[5], e.regs[6]
        loads.append((dst, sec, ln))
        e.write(dst, iso[base + sec * 2048:base + sec * 2048 + ln])
        e.regs[2] = 0
        e.pc = e.regs[31]

    def skip_cd(e):
        e.pc = e.regs[31]

    emu.hooks[0x8005D7D0] = cd_read
    emu.hooks[0x800787CC] = skip_cd

    for entry, nm in [(0x800102DC, "vol_init"), (0x80010228, "build_cache"),
                      (0x80010868, "task0b")]:
        try:
            emu.call(entry, sp=STACK_TOP, limit=30000000)
        except Trap as t:
            sys.exit(f"{nm} TRAP after {emu.steps} steps: {t}")
        print(f"{nm} ok steps={emu.steps}")
    print(f"cd reads: {len(loads)}")

    def rd16(addr):
        return struct.unpack("<H", emu.read(addr, 2))[0]

    def rbytes(addr, n):
        # chunked across the C/E page boundary
        out = b""
        while n:
            try:
                chunk = emu.read(addr, n)
            except Trap:
                end = 0x801E0000 if addr < 0x801E0000 else addr + n
                chunk = emu.read(addr, end - addr)
            out += chunk
            addr += len(chunk)
            n -= len(chunk)
        return out

    print("BOOTSTATE")
    car_count = rd16(0x801C93C8)
    print(f"  cars={car_count}")
    full = rbytes(0x801DF5D0, (car_count + 1) * 8)
    z = [struct.unpack_from("<H", full, i * 8 + 6)[0]
         for i in range(car_count)]
    print(f"  z_nonzero={sum(1 for v in z if v)} z149={z.count(149)} "
          f"z0={z[0]} z100={z[100]}")
    print(f"  cache6={rd16(0x801E2EF0 + 12)} "
          f"cache228={rd16(0x801E2EF0 + 456)} "
          f"cache229={rd16(0x801E2EF0 + 458)}")
    crs = emu.read(0x801E18E0, 8)
    print(f"  crs magic={crs[:4]!r} count={struct.unpack('<H', crs[6:8])[0]}")
    print(f"  taskmem0={emu.read(0x801C98E0, 1)[0]} "
          f"mark={struct.unpack('<I', emu.read(0x801CD554, 4))[0]:#x} "
          f"flag={emu.read(0x801C93C3, 1)[0]:#x} "
          f"span={struct.unpack('<I', emu.read(0x801C93C4, 4))[0]} "
          f"bounds={struct.unpack('<hh', emu.read(0x800A6F18, 4))}")


if __name__ == "__main__":
    main()
