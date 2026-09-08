#!/usr/bin/env python3
"""Emulate the car-logo annotator (task0b0 stage, 0x80011820).

Builds the carobj table with the REAL 0x80011710 under synthetic identity
weights (0x801EF630), then runs the REAL 0x80011820: 1673 /carlogo pairs
hashed, name[5] filter, bsearch annotation of the car table z-field,
backfill of misses with the first logo index. Counts stores by hooking
the store halfword (0x800118D0, emulated inline).

Ground truth for gt2_car_logo_annotate. True game weights are runtime
data (writer open); mechanics are weight-agnostic.

Usage: tools/car_logo.py [--iso PATH] [--scus PATH]
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
    emu.map(0x800A0000, bytearray(0xA0000))
    emu.write(0x800A97D0, iso[base + 0xB800:base + 0xB800 + 0x8C000])
    emu.map(0x801C0000, bytearray(0x20000))
    emu.map(0x801E0000, bytearray(0x20000))
    emu.map(0x801F0000, bytearray(0x10000))
    install_bios(emu)
    emu.write(0x801EF630, bytes(i & 0x3F for i in range(256)))
    emu.call(0x80011710, sp=STACK_TOP, limit=5000000)
    cnt = struct.unpack("<H", emu.read(0x801C93C8, 2))[0]
    print(f"carobj ok count={cnt}")

    names = []

    def rec(e):
        try:
            names.append(bytes(emu.read_cstr(e.regs[4], 32)))
        except Trap:
            names.append(b"?")
        e.regs[2] = 0
        e.pc = e.regs[31]

    emu.hooks[0x80060924] = rec
    emu.call(0x80011820, sp=STACK_TOP, limit=20000000)
    print(f"logo pass (stub hash): steps={emu.steps} hashed={len(names)} "
          f"first={names[0]!r} last={names[-1]!r}")
    del emu.hooks[0x80060924]

    stores = []

    def count_sh(e):
        e.write(e.regs[3] + 6, struct.pack("<H", e.regs[2] & 0xFFFF))
        stores.append(e.regs[3])
        e.pc = 0x800118D4

    # rebuild cleanly for the store count (pass above polluted z with 0-hash hits)
    emu2 = Emu(scus, vram_base=0x80010000, file_off=0x800)
    emu2.map(0x800A0000, bytearray(0xA0000))
    emu2.write(0x800A97D0, iso[base + 0xB800:base + 0xB800 + 0x8C000])
    emu2.map(0x801C0000, bytearray(0x20000))
    emu2.map(0x801E0000, bytearray(0x20000))
    emu2.map(0x801F0000, bytearray(0x10000))
    install_bios(emu2)
    emu2.write(0x801EF630, bytes(i & 0x3F for i in range(256)))
    emu2.call(0x80011710, sp=STACK_TOP, limit=5000000)
    emu2.hooks[0x800118D0] = count_sh
    emu2.call(0x80011820, sp=STACK_TOP, limit=20000000)
    print(f"logo pass (real hash): steps={emu2.steps} stores={len(stores)}")


if __name__ == "__main__":
    main()
