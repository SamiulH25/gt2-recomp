#!/usr/bin/env python3
"""Emulate gt2_02 menu-record routines (Phase F.1 ground truth).

Covers (VRAM addrs, gt2_02 loaded at 0x80010000 shadowing SCUS text):
  - record_init  0x80016394 (a0=dst, a1=src): pure field copy + defaults
  - counter_bump 0x800163C8 (a0=rec): saturating 0..12 counter at +0x10
  - emit_tick    0x80016410 (a0=rec, a1=fp, a2=flags-hi?, [sp+0x60]=t4):
      fixed-point ease head + packet-emit stanzas. The three SCUS
      callees are HOOKED (not executed):
        emit_A 0x80081478 / emit_B 0x8007DA44 (GPU packet arena appends;
          hooked: log args, return scratch packet pointer)
        0x8006B61C (short-path pair; hooked: log args, return 0)
      The wrapper 0x8001636C (inflate-index pair via gzSetup 0x80082FAC)
      is NOT emulated (needs SCUS gzip bytes + 0x7250 stack frames);
      documented gap, see decomp/docs/menu_notes.md.

Usage: tools/menu_tick.py [--scus PATH] [--ovr PATH]
Prints canonical vectors consumed by decomp/tests/test_menu.c.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, ".")
from tools.mips_emu import Emu, Trap
from tools.vol_walk import install_bios, STACK_TOP

REC_INIT = 0x80016394
COUNTER_BUMP = 0x800163C8
EMIT_TICK = 0x80016410
EMIT_A = 0x80081478
EMIT_B = 0x8007DA44
SHORT_CALLEE = 0x8006B61C

REC_ADDR = 0x801F4000
FP_ADDR = 0x801F5000
LIST_ADDR = 0x801F6000
PKT_ADDR = 0x801F7000


def fresh(scus, ovr):
    emu = Emu(scus, vram_base=0x80010000, file_off=0x800)
    emu.map(0x800A0000, bytearray(0xA8000))
    emu.map(0x801C0000, bytearray(0x20000))
    emu.map(0x801E0000, bytearray(0x20000))
    emu.map(0x801F0000, bytearray(0x10000))
    install_bios(emu)
    emu.write(0x80010000, ovr)   # overlay shadows SCUS text (as in-game)
    emu.write(REC_ADDR, bytes(0x40))
    emu.write(FP_ADDR, bytes(0x40))
    emu.write(LIST_ADDR, bytes(0x40))
    emu.write(PKT_ADDR, bytes(0x2000))
    return emu


class Hooks:
    def __init__(self, emu):
        self.emu = emu
        self.calls = []
        self.pkt_top = PKT_ADDR

    def install(self, emu):
        def mk(name, ret_pkt=False):
            def h(e):
                slot = -1
                if ret_pkt:
                    slot = self.pkt_top
                    self.pkt_top += 0x40
                    e.regs[2] = slot
                else:
                    e.regs[2] = 0
                if name == "S":
                    # cell block at a1: 4 halves + 2 words (24 B)
                    self.calls.append((name, e.regs[4], e.regs[5], slot,
                                       bytes(emu.read(e.regs[5], 24)).hex()))
                else:
                    self.calls.append((name, e.regs[4], e.regs[5], slot,
                                       ""))
                e.pc = e.regs[31]
            return h
        emu.hooks[EMIT_A] = mk("A", True)
        emu.hooks[EMIT_B] = mk("B", True)
        emu.hooks[SHORT_CALLEE] = mk("S", False)

    def reset(self, emu):
        # Scrub order-dependent state between vectors: guest stack slots
        # ([sp+0x20] s7 slot persists across same-SP calls in-game too)
        # and the scratch packet arena. Makes vectors reproducible.
        self.calls.clear()
        self.pkt_top = PKT_ADDR
        emu.write(0x801FF000, bytes(0x1000))
        emu.write(PKT_ADDR, bytes(0x2000))


def show_rec(emu, tag):
    raw = emu.read(REC_ADDR, 0x18)
    b0, b1, h2, h4, h6, b8 = struct.unpack_from("<BBh hh B", raw, 0)
    h10, h12 = struct.unpack_from("<hh", raw, 0x10)
    lst, = struct.unpack_from("<I", raw, 0x0C)
    print(f"  {tag}: b0={b0:#04x} b1={b1:#04x} h2={h2} h4={h4} h6={h6} "
          f"b8={b8:#04x} list={lst:#010x} cnt={h10} x12={h12}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scus", default="disc/SCUS_944.88")
    ap.add_argument("--ovr", default="overlays/gt2_02.exe")
    a = ap.parse_args()
    scus = open(a.scus, "rb").read()[0x800:]
    ovr = open(a.ovr, "rb").read()
    emu = fresh(scus, ovr)
    hk = Hooks(emu)
    hk.install(emu)

    print("== record_init vectors (src -> rec) ==")
    for src in (bytes([0x11, 0x22, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12,
                       0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x01, 0x02]),
                bytes(16)):
        emu.write(REC_ADDR, bytes(0x40))
        emu.write(FP_ADDR, src)
        try:
            emu.call(REC_INIT, a0=REC_ADDR, a1=FP_ADDR, sp=STACK_TOP,
                     limit=1000)
            show_rec(emu, f"src[0]={src[0]:#04x} ok steps={emu.steps}")
        except Trap as t:
            print(f"  TRAP: {t} pc={emu.pc:08x}")

    print("== counter_bump vectors (cnt_in -> cnt_out) ==")
    for cnt in (-3, -2, -1, 0, 1, 5, 11, 12, 13, 100):
        emu.write(REC_ADDR, bytes(0x40))
        emu.write_word(REC_ADDR + 0x0C, LIST_ADDR)
        raw = bytearray(emu.read(REC_ADDR, 0x18))
        struct.pack_into("<h", raw, 0x10, cnt)
        emu.write(REC_ADDR, bytes(raw))
        try:
            emu.call(COUNTER_BUMP, a0=REC_ADDR, sp=STACK_TOP, limit=1000)
            out, = struct.unpack("<h", emu.read(REC_ADDR + 0x10, 2))
            print(f"  cnt {cnt:4d} -> {out:4d}")
        except Trap as t:
            print(f"  cnt {cnt}: TRAP {t} pc={emu.pc:08x}")

    print("== emit_tick sweep (counter x flag0 x t4arg) ==")
    for cnt, fl, t4 in [(-1, 0, 0), (0, 0, 0), (3, 0, 0), (3, 1, 0),
                        (3, 3, 0), (3, 7, 0), (12, 0, 0), (-5, 0, 0),
                        (12, 1, 0), (12, 5, 0), (12, 7, 0), (3, 7, 0x99),
                        (3, 1, 0x99), (0, 5, 0), (11, 4, 0)]:
        emu.write(REC_ADDR, bytes(0x40))
        raw = bytearray(0x18)
        raw[0] = 0x40 | fl   # flag byte: bit6 set + low flag bits
        struct.pack_into("<h", raw, 2, 100)
        struct.pack_into("<h", raw, 4, 50)
        struct.pack_into("<h", raw, 6, 60)
        raw[8] = 0x33
        struct.pack_into("<I", raw, 0x0C, LIST_ADDR)
        struct.pack_into("<h", raw, 0x10, cnt)
        struct.pack_into("<h", raw, 0x12, 0x80)
        emu.write(REC_ADDR, bytes(raw))
        emu.write(LIST_ADDR, struct.pack("<IHHHH", 0xAABBCCDD,
                                         0x1111, 0x2222, 0x3333, 0x4444))
        emu.write(FP_ADDR, bytes([0x55]) + bytes(0x3F))
        hk.reset(emu)
        try:
            emu.call(EMIT_TICK, a0=REC_ADDR, a1=FP_ADDR, a2=t4,
                     sp=STACK_TOP, limit=20000)
            seq = " ".join(n for n, _, _, _, _ in hk.calls)
            print(f"  cnt={cnt:3d} fl={fl} t4={t4:#04x}: calls=[{seq}] "
                  f"n={len(hk.calls)} steps={emu.steps}")
            for n, a0, a1, _slot, cell in hk.calls:
                if cell:
                    print(f"      S cell {cell}")
                print(f"      {n} fp+{a0 - FP_ADDR:#x} flags={a1:#010x}")
            # Packet fills: 12 B at each hook-returned scratch pointer.
            for k, (n, _, _, slot, _) in enumerate(hk.calls):
                if n == "S":
                    continue
                pb = bytes(emu.read(slot, 12))
                print(f"      pkt{k} {pb.hex()}")
        except Trap as t:
            print(f"  cnt={cnt} fl={fl}: TRAP {t} pc={emu.pc:08x} "
                  f"calls={[n for n, _, _, _, _ in hk.calls]}")


if __name__ == "__main__":
    main()
