#!/usr/bin/env python3
"""Emulate the boot-task tails (task0b b2-b6) with a modeled RAM image.

Prefills RAM with 0xAA so every write is visible, seeds the 0x800A page
with real SCUS bytes (b3 reads static tables there), preloads the CRS
window for b3's phase-3 count, then executes the REAL task code:
  - b2 (0x80011CE4): descriptor store only — the 0x800787CC fill path is
    skipped via hook (it kicks a CD read through 0x8006830C: HW territory)
  - b3 (0x800104A0): all five phases, fully emulated (all callees pure)
  - b4/b5/b6: fully emulated

Reports per-task write regions. Ground truth for the gt2_task port.

Usage: tools/task_tail.py [--iso PATH] [--scus PATH] [--task b2|b3|b4|b5|b6|all]
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, ".")
from tools.mips_emu import Emu, Trap
from tools.vol_walk import install_bios, STACK_TOP

TASKS = {
    "b2": 0x80011CE4,
    "b3": 0x800104A0,
    "b4": 0x800107E8,
    "b5": 0x8001082C,
    "b6": 0x8001083C,
}
SEGS = ((0x800A0000, 0xA0000), (0x801C0000, 0x20000),
        (0x801E0000, 0x20000), (0x801F0000, 0x10000))
# NOTE: no 0x801D segment — 0x801Dxxxx addresses live in the 0x801C page's
# upper half; a separate map would shadow reads (caught 2026-09-08).


def fresh(scus_raw, iso):
    base = 473 * 2048
    scus = scus_raw[0x800:]
    emu = Emu(scus, vram_base=0x80010000, file_off=0x800)
    cover = bytearray(0xA0000)
    fstart = 0x800A0000 - 0x80010000 + 0x800
    favail = max(0, len(scus_raw) - fstart)
    cover[:favail] = scus_raw[fstart:fstart + favail]
    emu.map(0x800A0000, cover)
    emu.write(0x800A97D0, iso[base + 0xB800:base + 0xB800 + 0x8C000])
    for seg, sz in SEGS[1:]:
        emu.map(seg, bytearray(b"\xAA" * sz))
    emu.write(0x801E35F0, iso[base:base + 0xC000])
    emu.write(0x801E18E0, iso[base + 0x9C000:base + 0x9C000 + 0xFC5])
    install_bios(emu)
    return emu


def snap(emu):
    return {s: bytes(emu.read(s, z)) for s, z in SEGS}


def run(nm, scus_raw, iso):
    emu = fresh(scus_raw, iso)
    if nm == "b2":
        # Skip the CD-kick (0x800787CC -> 0x8006830C needs the CD driver);
        # the descriptor store is the portable prefix.
        def skip_cd(e):
            e.pc = e.regs[31]
        emu.hooks[0x800787CC] = skip_cd
    before = snap(emu)
    try:
        emu.call(TASKS[nm], sp=STACK_TOP, limit=20000000)
        res = f"ok steps={emu.steps}"
    except Trap as t:
        res = f"TRAP {t} steps={emu.steps} pc={emu.pc:#x}"
    print(f"== {nm} ({TASKS[nm]:#x}): {res}")
    after = snap(emu)
    nbytes, nreg = 0, 0
    for seg in before:
        a, b = before[seg], after[seg]
        i, n = 0, len(a)
        while i < n:
            if a[i] != b[i]:
                j = i
                while j < n and a[j] != b[j]:
                    j += 1
                # skip stack noise
                if not (0x801FFE00 <= seg + i < 0x80200000):
                    print(f"   {seg + i:#x} +{j - i} ({j - i:#x})")
                    nreg += 1
                nbytes += j - i
                i = j
            else:
                i += 1
    print(f"   touched: {nbytes} bytes in {nreg} regions (stack excluded)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=os.environ.get("GT2_ISO", "/tmp/opencode/gt2.iso"))
    ap.add_argument("--scus", default="disc/SCUS_944.88")
    ap.add_argument("--task", default="all")
    a = ap.parse_args()
    iso = open(a.iso, "rb").read()
    scus_raw = open(a.scus, "rb").read()
    names = [a.task] if a.task != "all" else list(TASKS)
    for nm in names:
        run(nm, scus_raw, iso)


if __name__ == "__main__":
    main()
