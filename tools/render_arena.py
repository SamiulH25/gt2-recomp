#!/usr/bin/env python3
"""Emulate SCUS GPU-packet emitters (Phase F render groundwork).

  - emit_B 0x8007DA44 (a0=fp, a1=flags): 8-B packet at arena top
    [tag 1, link-word, flags|0xE1000000], top += 8, returns old+4.
  - emit_A 0x80081478 (a0=fp, a1=flags): same with tag 4,
    flags^0x64000000, top += 0x14, returns old+8.
Arena cell [0x801C93EC] (u32 top). Both use unaligned lwl/swl for the
link word (fp+2 <-> packet+2); the caller's stale a2 is dropped by the
swl (proven by an a2-sweep vector here).

The tick fills 12 B at A-returns: [r, r+12) with next top exactly
r+12 (A) — perfect tiling, no overlap. B-returns are never filled.

Usage: tools/render_arena.py [--scus PATH]
Prints canonical vectors consumed by decomp/tests/test_render.c.
"""
import argparse
import struct
import sys

sys.path.insert(0, ".")
from tools.mips_emu import Emu, Trap
from tools.vol_walk import install_bios, STACK_TOP

EMIT_A = 0x80081478
EMIT_B = 0x8007DA44
CELL = 0x801C93EC
FP = 0x801F4000
ARENA = 0x801F7000


def fresh(scus):
    emu = Emu(scus, vram_base=0x80010000, file_off=0x800)
    emu.map(0x800A0000, bytearray(0xA8000))
    emu.map(0x801C0000, bytearray(0x20000))
    emu.map(0x801E0000, bytearray(0x20000))
    emu.map(0x801F0000, bytearray(0x10000))
    install_bios(emu)
    emu.write(FP, bytes(0x40))
    emu.write(ARENA, bytes(0x1000))
    return emu


def run(emu, entry, fp_off, top_off, flags, a2, link4):
    emu.write(FP, bytes(0x40))
    emu.write(FP + fp_off, link4)
    emu.write(ARENA, bytes(0x1000))
    emu.write_word(CELL, ARENA + top_off)
    try:
        r = emu.call(entry, a0=FP + fp_off - 2, a1=flags, a2=a2,
                     sp=STACK_TOP, limit=1000)
        top1 = struct.unpack("<I", emu.read(CELL, 4))[0]
        pkt = bytes(emu.read(ARENA + top_off, 24)).hex()
        fpw = bytes(emu.read(FP + fp_off - 2, 6)).hex()
        print(f"  fp+{fp_off} top+{top_off} fl={flags:#010x} a2={a2:#010x} "
              f"link={link4.hex()}: ret+{r - (ARENA + top_off):#x} "
              f"top1+{top1 - ARENA:#x}")
        print(f"    pkt {pkt}")
        print(f"    fp  {fpw}")
    except Trap as t:
        print(f"  TRAP {t} pc={emu.pc:08x}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scus", default="disc/SCUS_944.88")
    a = ap.parse_args()
    scus = open(a.scus, "rb").read()[0x800:]
    emu = fresh(scus)

    print("== emit_B alignment matrix (flags 0x3333, link deadbeef) ==")
    for fp_off in (2, 3, 4, 5):
        for top_off in (0, 1, 2, 3):
            run(emu, EMIT_B, fp_off, top_off, 0x3333, 0xAAAAAAAA,
                bytes.fromhex("deadbeef"))

    print("== emit_B flags/link/a2 sweep (aligned) ==")
    run(emu, EMIT_B, 2, 0, 0x0, 0x0, bytes.fromhex("00000000"))
    run(emu, EMIT_B, 2, 0, 0xFFFFFFFF, 0x12345678,
        bytes.fromhex("11223344"))
    # a2 stale-byte irrelevance: same vector, different a2 low byte.
    run(emu, EMIT_B, 2, 0, 0x3333, 0xFFFFFF00, bytes.fromhex("deadbeef"))
    run(emu, EMIT_B, 2, 0, 0x3333, 0xFFFFFF7F, bytes.fromhex("deadbeef"))

    print("== emit_A alignment matrix (flags 0x3333, link deadbeef) ==")
    for fp_off in (2, 3, 4, 5):
        for top_off in (0, 1, 2, 3):
            run(emu, EMIT_A, fp_off, top_off, 0x3333, 0xAAAAAAAA,
                bytes.fromhex("deadbeef"))

    print("== emit_A flags/link sweep (aligned) ==")
    run(emu, EMIT_A, 2, 0, 0x0, 0x0, bytes.fromhex("00000000"))
    run(emu, EMIT_A, 2, 0, 0xFFFFFFFF, 0x12345678,
        bytes.fromhex("11223344"))


if __name__ == "__main__":
    main()
