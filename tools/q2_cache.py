#!/usr/bin/env python3
"""Emulate the Q2 index-cache builder (0x80010228) with a modeled RAM image.

Replicates the VOL init memory effects, then executes the REAL builder
over the 248 boot paths (SCUS 0x8009118C, NUL-pointer terminated):
hit -> tbl index (dirs descend one slot past '..' and take that entry),
miss -> 0xFFFF. Dumps the cache for the gt2_q2_cache_build port.

Usage: tools/q2_cache.py [--iso PATH] [--scus PATH]
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
    scus_raw = open(a.scus, "rb").read()
    scus, base = scus_raw[0x800:], 473 * 2048

    emu = Emu(scus, vram_base=0x80010000, file_off=0x800)
    emu.map(0x800A0000, bytearray(0xA0000))
    emu.write(0x800A97D0, iso[base + 0xB800:base + 0xB800 + 0x8C000])
    emu.map(0x801C0000, bytearray(0x20000))
    emu.map(0x801E0000, bytearray(0x20000))
    emu.map(0x801F0000, bytearray(0x10000))
    install_bios(emu)
    try:
        emu.call(0x80010228, sp=STACK_TOP, limit=30000000)
    except Trap as t:
        sys.exit(f"build_cache TRAP after {emu.steps} steps: {t}")
    print(f"build_cache ok steps={emu.steps}")

    cache = struct.unpack("<248H", emu.read(0x801E2EF0, 248 * 2))
    ptrs = struct.unpack_from("<248I", scus_raw,
                              0x800 + (0x8009118C - 0x80010000))
    paths = [scus_raw[0x800 + p - 0x80010000:][:64].split(b"\x00")[0].decode()
             for p in ptrs if p]
    print(f"paths={len(paths)} pinned={sum(1 for v in cache if v != 0xFFFF)}")
    for i in (0, 6, 8, 70, 107, 108, 191, 228, 229):
        print(f"  [{i}] cache={cache[i]} {paths[i]}")
    print("  misses:", [i for i, v in enumerate(cache) if v == 0xFFFF])


if __name__ == "__main__":
    main()
