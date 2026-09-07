#!/usr/bin/env python3
"""Emulate gt2_load_overlay_default(idx) end to end (replayable).

Stubs: BIOS A-table, CdReadSector (real ISO), CdReadByName/0x8007AD90
(serves the real OVL member to staging + returns the member index in
a0), gzSetup (real zlib inflate to the model's VRAM, verified against
overlays/*.exe), SPU/FlushCache/memzero no-ops.

Proven: idx0 -> 316920 B (m0), idx1 -> 248004 B (m1), both byte-exact
at 0x80010000. Phase-A gunzip failure (empty staging) is tolerated
(v0=-1, flow continues). A third DAD8 pass after the successful
inflate walks off the modeled table (dispatch-table fill = open item,
see decomp/docs/overlay_notes.md).

Usage: tools/ovl_load.py [idx] [--iso PATH]
"""
import argparse
import gzip
import os
import struct
import sys

sys.path.insert(0, ".")
from tools.mips_emu import Emu, Trap
from tools.vol_walk import install_bios, STACK_TOP

STAGING = 0x800A8D5C
TABLE = 0x801EF610


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("idx", nargs="?", type=int, default=1)
    ap.add_argument("--iso", default=os.environ.get("GT2_ISO", "/tmp/opencode/gt2.iso"))
    ap.add_argument("--scus", default="disc/SCUS_944.88")
    a = ap.parse_args()

    iso = open(a.iso, "rb").read()
    scus = open(a.scus, "rb").read()[0x800:]
    emu = Emu(scus, vram_base=0x80010000, file_off=0x800)
    emu.map(0x800A0000, bytearray(0x100000))   # staging (shadows SCUS tail)
    emu.map(0x801C0000, bytearray(0x10000))    # save area / mailbox
    emu.map(0x801E0000, bytearray(0x20000))
    emu.map(0x801F0000, bytearray(0x10000))
    install_bios(emu)

    off = 331 * 2048
    vals = struct.unpack("<12I", iso[off:off + 48])
    starts = [0x30, vals[2], vals[4], vals[6], vals[8], vals[10]]
    comps = [vals[1], vals[3], vals[5], vals[7], vals[9], vals[11]]
    ref = [open(f"overlays/gt2_0{k + 1}.exe", "rb").read() for k in range(6)]
    scusd = open(a.scus, "rb").read()
    tbase = 0x800 + (0x80091174 - 0x80010000)
    desc2idx = {struct.unpack_from("<I", scusd, tbase + i * 4)[0]: i
                for i in range(6)}

    def ad90(e):
        mbox, desc = e.regs[4], e.regs[5]
        idx = desc2idx.get(desc, 0)
        e.write(STAGING, iso[off + starts[idx]:off + starts[idx] + comps[idx]])
        print(f"  AD90 desc={desc:#x} -> member {idx} to staging")
        e.regs[4] = idx
        e.regs[2] = 0
        e.pc = e.regs[31]

    def gz(e):
        src, dst = e.regs[4], e.regs[5]
        if bytes(emu.read(src, 2)) != b"\x1f\x8b":
            print(f"  gzSetup not-gzip at {src:#x} -> v0=-1")
            e.regs[2] = 0xFFFFFFFF
            e.pc = e.regs[31]
            return
        d = gzip.decompress(bytes(emu.read(src, 150000)))
        emu.write(dst, d)
        ok = next((k for k in range(6) if d == ref[k]), None)
        print(f"  gzSetup inflated {len(d)}B member={ok} -> {dst:#x}")
        e.regs[2] = 0
        e.pc = e.regs[31]

    def cdsec(e):
        dst, lba, ln = e.regs[4], e.regs[5], e.regs[6]
        e.write(dst, iso[lba * 2048:lba * 2048 + ln])
        e.regs[2] = 0
        e.pc = e.regs[31]

    emu.hooks[0x8007AD90] = ad90
    emu.hooks[0x80082FAC] = gz
    emu.hooks[0x8007AB74] = cdsec
    for addr in [0x80078370, 0x800783DC, 0x8008C908, 0x8005D9BC,
                 0x800847D0, 0x8007AD20]:
        def mk(e, addr=addr):
            e.regs[2] = 0
            e.pc = e.regs[31]
        emu.hooks[addr] = mk

    print(f"== load_overlay_default({a.idx}) ==")
    try:
        ret = emu.call(0x8005DA3C, a0=a.idx, sp=STACK_TOP, limit=200000)
        print(f"  returned {ret:#x} steps={emu.steps}")
    except Trap as t:
        print(f"  TRAP after {emu.steps}: {t} "
              f"(post-load pass; dispatch fill open)")


if __name__ == "__main__":
    main()
