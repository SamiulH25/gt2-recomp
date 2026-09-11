#!/usr/bin/env python3
"""Emulate task0b b2 (0x80011CE4) WITHOUT skipping its CD-kick.

Harness = tools/boot_chain.py through build_cache (real vol_init +
Q2 cache), then b2 with the real sector reader 0x8005D7D0 serving from
the image. CD-HW leaves (0x8007CFDC/0x8007AB14/0x8007D024) are logged;
if reached, they are skipped (return 0) — the experiment then shows how
far the portable prefix goes.

Goal: ground truth for the native b2 port (descriptor values, heap
delta at 0x80092E74, deposited bytes at 0x801E2CE0/0x801E2CF0).
Ported as gt2_task_b2_queue/complete (see gt2/task.h); this tool stays
as the replayable harness. Needs `neg` in tools/mips_emu.py (added).

Usage: tools/b2_kick.py [--iso PATH] [--scus PATH]
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, ".")
from tools.mips_emu import Emu, Trap
from tools.vol_walk import install_bios, STACK_TOP  # noqa: E402

LOADS = []


def fresh(scus, iso):
    emu = Emu(scus, vram_base=0x80010000, file_off=0x800)
    emu.map(0x800A0000, bytearray(0xA8000))
    emu.map(0x801C0000, bytearray(0x20000))
    emu.map(0x801E0000, bytearray(0x20000))
    emu.map(0x801F0000, bytearray(0x10000))
    install_bios(emu)
    emu.write_word(0x801C93E8, 473)   # VOL LBA cell (ISO-init effect)
    return emu


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=os.environ.get("GT2_ISO",
                                                    "/tmp/opencode/gt2.iso"))
    ap.add_argument("--scus", default="disc/SCUS_944.88")
    a = ap.parse_args()

    iso = open(a.iso, "rb").read()
    scus_raw = open(a.scus, "rb").read()
    base = 473 * 2048
    emu = fresh(scus_raw[0x800:], iso)

    def cd_read(e):
        dst, sec, ln = e.regs[4], e.regs[5], e.regs[6]
        LOADS.append((dst, sec, ln))
        e.write(dst, iso[base + sec * 2048:base + sec * 2048 + ln])
        e.regs[2] = 0
        e.pc = e.regs[31]

    def hw_leaf(name):
        def h(e):
            print(f"HW-LEAF {name} "
                  f"a0={e.regs[4]:08x} a1={e.regs[5]:08x} "
                  f"a2={e.regs[6]:08x} a3={e.regs[7]:08x}")
            e.pc = e.regs[31]
            e.regs[2] = 0
        return h

    emu.hooks[0x8005D7D0] = cd_read
    for leaf in (0x8007CFDC, 0x8007AB14, 0x8007D024):
        emu.hooks[leaf] = hw_leaf(f"{leaf:08x}")

    for entry, nm in [(0x800102DC, "vol_init"),
                      (0x80010228, "build_cache")]:
        try:
            emu.call(entry, sp=STACK_TOP, limit=30000000)
        except Trap as t:
            sys.exit(f"{nm} TRAP after {emu.steps} steps: {t}")
        print(f"{nm} ok steps={emu.steps}")

    print(f"cache247={struct.unpack('<H', emu.read(0x801E2EF0 + 494, 2))[0]} "
          f"heap0={struct.unpack('<I', emu.read(0x80092E74, 4))[0]}")
    try:
        emu.call(0x80011CE4, sp=STACK_TOP, limit=20000000)
        print(f"b2 returned steps={emu.steps}")
    except Trap as t:
        print(f"b2 TRAP after {emu.steps} steps: {t} pc={emu.pc:08x}")
    print(f"sector loads: {len(LOADS)}")
    for dst, sec, ln in LOADS[:8]:
        print(f"  dst={dst:08x} sector={sec} len={ln}")
    print("desc @801E2CE0:",
          [f"{v:08x}" for v in struct.unpack("<3I", emu.read(0x801E2CE0, 12))])
    print("heap @80092E74:",
          struct.unpack("<I", emu.read(0x80092E74, 4))[0])
    print("data @801E2CF0 head:", emu.read(0x801E2CF0, 32).hex())


if __name__ == "__main__":
    main()
