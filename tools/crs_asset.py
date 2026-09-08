#!/usr/bin/env python3
"""Emulate the course-asset loaders (task0b1) with a modeled RAM image.

Replicates the VOL init memory effects (vol_buffer + header copy at
0x801E35F0), pins cache[6] = tbl 8 (/.crsinfo, as build_cache would), serves
CD reads from the real image, then executes the REAL task0b1 (0x80011C70):
  - 0x80011B70 builds the /crsmap stem-hash table (RAM 0x801E33F0)
  - 0x8005D8A0 loads cache slot 6 to 0x801E18E0 (sector-aligned window)
  - the tail loop relocates the CRS records by the load base

Reports the hash table (count/first/entries), the window (base/len/magic),
and the relocated records. Ground truth for the gt2_asset port.

Usage: tools/crs_asset.py [--iso PATH] [--scus PATH]
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, ".")
from tools.mips_emu import Emu, Trap
from tools.vol_walk import install_bios, STACK_TOP

VOL_BASE_SECTOR = 473
VOL_BUF = 0x800A97D0
VOL_READ = 0x8C000
HDR_COPY = 0x801E35F0
HDR_SIZE = 0xC000
LBA_CELL = 0x801C93E8
CACHE = 0x801E2EF0
LOAD_BASE = 0x801E18E0
HASH_TAB = 0x801E33F0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=os.environ.get("GT2_ISO", "/tmp/opencode/gt2.iso"))
    ap.add_argument("--scus", default="disc/SCUS_944.88")
    a = ap.parse_args()
    iso = open(a.iso, "rb").read()
    scus = open(a.scus, "rb").read()[0x800:]
    base = VOL_BASE_SECTOR * 2048

    emu = Emu(scus, vram_base=0x80010000, file_off=0x800)
    emu.map(0x800A0000, bytearray(0xA0000))
    emu.write(VOL_BUF, iso[base + 0xB800:base + 0xB800 + VOL_READ])
    emu.map(0x801C0000, bytearray(0x20000))
    emu.map(0x801E0000, bytearray(0x20000))
    emu.map(0x801F0000, bytearray(0x10000))
    emu.write(HDR_COPY, iso[base:base + HDR_SIZE])
    install_bios(emu)

    loads = []

    def cd_stub(e):
        dst, absoff, ln = e.regs[4], e.regs[5], e.regs[6]
        loads.append((dst, absoff, ln))
        e.write(dst, iso[absoff:absoff + ln])
        e.regs[2] = 0
        e.pc = e.regs[31]

    emu.hooks[0x8007AB78] = cd_stub
    emu.hooks[0x8007AD20] = cd_stub
    emu.write_word(LBA_CELL, VOL_BASE_SECTOR)
    c = bytearray(emu.read(CACHE, 32))
    c[12:14] = struct.pack("<H", 8)   # cache[6] = tbl 8 (/.crsinfo)
    emu.write(CACHE, bytes(c))

    try:
        emu.call(0x80011C70, sp=STACK_TOP, limit=10000000)
    except Trap as t:
        sys.exit(f"task0b1 TRAP after {emu.steps} steps: {t}")
    print(f"task0b1 ok steps={emu.steps}")
    for dst, off, ln in loads:
        print(f"  cd load: dst={dst:#x} abs={off:#x} len={ln:#x} ({ln})")

    first = struct.unpack("<H", emu.read(0x801C93DC, 2))[0]
    nmap = struct.unpack("<H", emu.read(0x801C93E4, 2))[0]
    print(f"crsmap: count={nmap} first_tbl={first}")
    hashes = struct.unpack(f"<{nmap}I", emu.read(HASH_TAB, nmap * 4))
    print(f"  hash[0]={hashes[0]:#x} hash[-1]={hashes[-1]:#x}")

    mem = emu.read(LOAD_BASE, loads[0][2] if loads else 0xFC5)
    cnt = struct.unpack_from("<H", mem, 6)[0]
    print(f"crsinfo: magic={mem[:4]!r} count={cnt}")
    bad = 0
    for i in range(cnt):
        ptr = struct.unpack_from("<I", mem, 8 + i * 0x18)[0] - LOAD_BASE
        if not 0 <= ptr < len(mem):
            print(f"  rec{i} OUT OF WINDOW off={ptr:#x}")
            bad += 1
    print(f"  ptrs in-window: {cnt - bad}/{cnt}")
    h0 = struct.unpack_from("<I", mem, 12)[0]
    print(f"  rec0 hash={h0:#x} (crsmap[0]={hashes[0]:#x} match={h0 == hashes[0]})")
    p0 = struct.unpack_from("<I", mem, 8)[0] - LOAD_BASE
    print(f"  rec0 target: {mem[p0:p0 + 24]!r}")


if __name__ == "__main__":
    main()
