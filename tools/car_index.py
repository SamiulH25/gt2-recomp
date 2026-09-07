#!/usr/bin/env python3
"""Emulate the car data index loaders (task0b0 stages) with a fully
resident slot table (idealized paging).

Reports per loader: entries hashed (first/last), RAM table range,
count cell. Ground truth for the gt2_car port.

Usage: tools/car_index.py [--iso PATH] [--loader carobj|carlogo|carwheel|engine|all]
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, ".")
from tools.mips_emu import Emu, Trap
from tools.vol_walk import install_bios, STACK_TOP

LOADERS = {
    "carobj": 0x80011710,
    "carlogo": 0x80011820,
    "carwheel": 0x8001194C,
    "engine": 0x80011A10,
}


def fresh(scus, iso):
    base = 473 * 2048
    emu = Emu(scus, vram_base=0x80010000, file_off=0x800)
    emu.map(0x800A0000, bytearray(0xA0000))
    emu.write(0x800A97D0, iso[base + 0xB800:base + 0xB800 + 0x8C000])
    emu.map(0x801C0000, bytearray(0x20000))
    emu.map(0x801E0000, bytearray(0x20000))
    emu.map(0x801F0000, bytearray(0x10000))
    install_bios(emu)

    def hash_stub(e):
        e.regs[2] = 0
        e.pc = e.regs[31]

    emu.hooks[0x80060924] = hash_stub
    return emu


def run_loader(emu, entry, want_names=False):
    names = []

    def hash_stub(e):
        try:
            names.append(emu.read_cstr(e.regs[4], 32))
        except Trap:
            names.append(b"?")
        e.regs[2] = 0
        e.pc = e.regs[31]

    emu.hooks[0x80060924] = hash_stub
    before = {}
    for seg, n in ((0x801C0000, 0x20000), (0x801E0000, 0x20000)):
        before[seg] = bytes(emu.read(seg, n))
    try:
        emu.call(entry, sp=STACK_TOP, limit=3000000)
        res = f"ok steps={emu.steps}"
    except Trap as t:
        res = f"TRAP {t} steps={emu.steps}"
    writes = {}
    for seg, n in ((0x801C0000, 0x20000), (0x801E0000, 0x20000)):
        after = bytes(emu.read(seg, n))
        a, b = before[seg], after
        i, regions = 0, []
        while i < n:
            if a[i] != b[i]:
                j = i
                while j < n and a[j] != b[j]:
                    j += 1
                regions.append((seg + i, j - i))
                i = j
            else:
                i += 1
        writes[seg] = regions
    return res, names, writes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=os.environ.get("GT2_ISO", "/tmp/opencode/gt2.iso"))
    ap.add_argument("--scus", default="disc/SCUS_944.88")
    ap.add_argument("--loader", default="all")
    a = ap.parse_args()
    iso = open(a.iso, "rb").read()
    scus = open(a.scus, "rb").read()[0x800:]
    names = [a.loader] if a.loader != "all" else list(LOADERS)
    for nm in names:
        emu = fresh(scus, iso)
        res, hashed, writes = run_loader(emu, LOADERS[nm])
        print(f"== {nm}: {res}, hashed={len(hashed)}")
        if hashed:
            print(f"   first={hashed[0]!r} last={hashed[-1]!r}")
        for seg, regions in writes.items():
            print(f"   writes@{seg:#x}: {[(hex(x), n) for x, n in regions][:8]}")
        if nm == "carobj":
            # dump the RAM table for xcheck vs the C port (chunked:
            # the table straddles the C/E page mapping boundary)
            tab = emu.read(0x801DF5D0, 0x801E0000 - 0x801DF5D0)
            tab += emu.read(0x801E0000, 1110 * 8 - len(tab))
            with open("/tmp/opencode/carobj_emu.bin", "wb") as f:
                f.write(tab)
            print("   wrote /tmp/opencode/carobj_emu.bin")


if __name__ == "__main__":
    main()
