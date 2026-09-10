#!/usr/bin/env python3
"""Emulate the SCUS card state machine (Phase F save ground truth).

Entry 0x80073720 (a0=state, a1=evbits; s1 = status bits from the caller
— preset here with a one-shot hook since emu.call zeroes regs). Exits
via 0x80073AD8 (v0 = s3 in {-2,-3}) or 0x80073ADC. State fields:
+0x14 counter (menu-style saturate), +0x1A/+0x1B bytes, +0x1C half,
+0x24 word, +0x28/+0x44 blocks, +0x4A half. Callees (all hooked, log +
return 0): 0x80060840, 0x8006AC68/90, 0x8006AD3C, 0x8006BE64/F4,
0x8006CFC4, 0x8006D400, 0x8006D50C, 0x80073524, 0x8007DA44/80,
0x8008CFC4.

Usage: tools/save_state.py [--scus PATH]
Prints canonical vectors consumed by decomp/tests/test_card.c.
"""
import argparse
import struct
import sys

sys.path.insert(0, ".")
from tools.mips_emu import Emu, Trap
from tools.vol_walk import install_bios, STACK_TOP

ENTRY = 0x80073720
STATE = 0x801F4000
SCRATCH = 0x801F6000   # +0x24 data pointer target (mapped, patterned)
EV = 0x801F5000   # event struct: status bits @+4, extra bits @+0xC
                  # (entry-a1 is a POINTER, not flags — small ints trap)

CALLEES = [0x80060840, 0x8006AC68, 0x8006AC90, 0x8006AD3C, 0x8006BE64,
           0x8006BEF4, 0x8006CFC4, 0x8006D400, 0x8006D50C, 0x80073524,
           0x8007DA44, 0x8007DA80, 0x8008CFC4]


def fresh(scus):
    emu = Emu(scus, vram_base=0x80010000, file_off=0x800)
    emu.map(0x800A0000, bytearray(0xA8000))
    emu.map(0x801C0000, bytearray(0x20000))
    emu.map(0x801E0000, bytearray(0x20000))
    emu.map(0x801F0000, bytearray(0x10000))
    install_bios(emu)
    emu.write(STATE, bytes(0x100))
    return emu


class Hooks:
    def __init__(self):
        self.calls = []
        self.scripts = {}   # addr -> [returns]
        self.script_i = {}

    def install(self, emu):
        for addr in CALLEES:
            def mk(a=addr):
                def h(e):
                    if a in self.scripts:
                        i = self.script_i.get(a, 0)
                        seq = self.scripts[a]
                        v = seq[min(i, len(seq) - 1)]
                        self.script_i[a] = i + 1
                    else:
                        v = 0
                    self.calls.append((f"{a:08x}", e.regs[4], e.regs[5],
                                       v))
                    e.regs[2] = v & 0xFFFFFFFF
                    e.pc = e.regs[31]
                return h
            emu.hooks[addr] = mk()

    def reset(self, emu):
        self.calls.clear()
        self.script_i.clear()
        emu.write(0x801FF000, bytes(0x1000))
        emu.write(STATE, bytes(0x100))
        emu.write(SCRATCH, bytes(0x100))
        emu.write(STATE + 0x24, struct.pack("<I", SCRATCH))


def run(emu, hk, s1, ev4, evC, fields, tag, scripts=None):
    hk.reset(emu)
    hk.scripts = dict(scripts or {})
    for off, fmt, val in fields:
        emu.write(STATE + off, struct.pack(fmt, val))
    if tag.startswith("ad3c"):
        emu.write(SCRATCH, bytes(range(0x20)))
    emu.write(EV, bytes(0x20))
    emu.write(EV + 4, struct.pack("<I", ev4))
    emu.write(EV + 0xC, struct.pack("<I", evC))
    # one-shot s1 preset (self-deleting so entry executes normally)
    def preset(e, v=s1):
        e.regs[17] = v & 0xFFFFFFFF
        del emu.hooks[ENTRY]
    emu.hooks[ENTRY] = preset
    try:
        r = emu.call(ENTRY, a0=STATE, a1=EV, sp=STACK_TOP,
                     limit=50000)
        seq = " ".join(n for n, _, _, _ in hk.calls)
        st = emu.read(STATE, 0x50)
        print(f"  {tag}: ret={r - 2**32 if r > 2**31 else r} "
              f"calls=[{seq}] n={len(hk.calls)} steps={emu.steps}")
        args = " ".join(f"{n}:{a0:#x},{a1:#x}" for n, a0, a1, v in hk.calls
                        if n in ("80060840", "8006d400", "8006ad3c",
                                 "80073524", "8008cfc4"))
        if args:
            print(f"    args {args}")
        print(f"    state14={struct.unpack('<h', st[0x14:0x16])[0]} "
              f"16={struct.unpack('<h', st[0x16:0x18])[0]} "
              f"18={struct.unpack('<h', st[0x18:0x1A])[0]} "
              f"1a={st[0x1A]:02x} 1b={st[0x1B]:02x} "
              f"1c={struct.unpack('<h', st[0x1C:0x1E])[0]} "
              f"4a={struct.unpack('<h', st[0x4A:0x4C])[0]}")
        if tag.startswith("ad3c"):
            print(f"    scratch={bytes(emu.read(SCRATCH, 16)).hex()}")
    except Trap as t:
        print(f"  {tag}: TRAP {t} pc={emu.pc:08x} "
              f"calls={[n for n, _, _, _ in hk.calls]}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scus", default="disc/SCUS_944.88")
    a = ap.parse_args()
    scus = open(a.scus, "rb").read()[0x800:]
    emu = fresh(scus)
    hk = Hooks()
    hk.install(emu)

    print("== status-bit sweep (state zeroed) ==")
    for s1, ev4, evC, tag in [(0, 0, 0, "idle"), (0, 1, 0, "ev4-1"),
                              (1, 0, 0, "st1"), (0, 0x10, 0, "ev10"),
                              (0, 0x1000, 0, "ev1000"),
                              (0, 4, 0, "ev4"), (0, 8, 0, "ev8"),
                              (0, 0x500, 0, "ev500"),
                              (0, 0xA00, 0, "evA00"),
                              (0x101F, 0x101F, 0x101F, "all")]:
        run(emu, hk, s1, ev4, evC, [], tag)

    print("== card regions (ev/fields/scripts) ==")
    # bit0x10000 -> D400 block; +0x1C>=1 x 0x500 -> R2 countdown;
    # +0x1A==0xC x 0xA00 -> 8CFC4 path; 6AD3C-ret steering.
    for s1, ev4, evC, fields, scripts, tag in [
            (0, 0x10000, 0, [], None, "bit10000"),
            (0, 0x500, 0, [(0x1C, "<h", 3)], None, "r2count"),
            (0, 0x500, 0, [(0x1C, "<h", 0)], None, "r2zero"),
            (0, 0xA00, 0, [(0x1A, "<B", 0xC), (0x1B, "<B", 3),
                           (0x1E, "<h", 5)], {0x8008CFC4: [9]},
             "a00-1aC"),
            (0, 0xA00, 0, [(0x1A, "<B", 0xC), (0x1B, "<B", 9)],
             None, "a00-1aC-hi1b"),
            (0, 0xA00, 0, [(0x1A, "<B", 0x5)], None, "a00-1a5"),
            (0, 0xA00, 0, [(0x1C, "<h", 4), (0x20, "<h", 10),
                           (0x1E, "<h", 20)], {0x8008CFC4: [9],
             0x8006AD3C: [3]}, "ad3c-low"),
            (0, 0xA00, 0, [(0x1C, "<h", 4), (0x20, "<h", 2),
                           (0x1E, "<h", 20)], {0x8008CFC4: [9],
             0x8006AD3C: [7]}, "ad3c-high"),
            ]:
        run(emu, hk, s1, ev4, evC, fields, tag, scripts)

    print("== counter edges (+0x14/+0x16/+0x18) ==")
    for fields, tag in [([(0x14, "<h", 0x28)], "c14-40"),
                        ([(0x14, "<h", 11)], "c14-11"),
                        ([(0x16, "<h", 59)], "c16-59"),
                        ([(0x16, "<h", 0x3C)], "c16-60"),
                        ([(0x16, "<h", 0x3D)], "c16-61"),
                        ([(0x18, "<H", 0x10)], "c18-16"),
                        ([(0x18, "<H", 40)], "c18-40")]:
        run(emu, hk, 0, 0, 0, fields, tag)

    print("== state-field sweep (ev 0x101F/0x101F) ==")
    for fields, tag in [([(0x14, "<h", 5)], "cnt5"),
                        ([(0x14, "<h", -3)], "cntNeg"),
                        ([(0x1C, "<h", 7)], "h1c"),
                        ([(0x1B, "<B", 3)], "b1b"),
                        ([(0x1A, "<B", 0xC)], "b1a"),
                        ([(0x4A, "<h", 9)], "h4a"),
                        ([(0x24, "<I", 0x801F4100)], "w24")]:
        run(emu, hk, 0, 0x101F, 0x101F, fields, tag)


if __name__ == "__main__":
    main()
