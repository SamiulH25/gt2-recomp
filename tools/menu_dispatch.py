#!/usr/bin/env python3
"""Emulate gt2_01 menu dispatcher cluster (Phase F.1b ground truth).

Covers (VRAM addrs, gt2_01 loaded at 0x80010000 shadowing SCUS text):
  - dispatch    0x80017784 (a0=obj): guards (obj+0x5D0, task mode byte)
      + 10-way jr into TABLE1 (0x8002F0B8, runtime-filled: seeded here)
      + arms A/B/C/D (helpers + flag byte +0x408) + mark-table tail.
      Returns 2/4/7/9/11.
  - sync_0      0x8001710C (a0=obj,a1=v,a2=n): pure task-mem/object
      field shuffle + stamp loop. No hooks needed.
  - wait0/wait1 0x80017174 / 0x800171B8 (a0=obj): set +0x38B to 0/1,
      poll 0x800833E8(obj+0x304) until 0/1 (hooked: scripted).
  - subdispatch 0x80017200 (a0=obj,a1=flag?,a2=?): 11-way jr into
      TABLE2 (0x8002F058, seeded) + worker/poll stanzas. Workers
      0x800472D4 / 0x8004DF34 are file-NOPs (runtime-patched): hooked.

Task base 0x801C98E0 (a3): mode byte at +0xBF86 (lbu), halves at
+0x582/+0x584, writeback at +0x58, mark area +0x3C74 (b3's).

Usage: tools/menu_dispatch.py [--scus PATH] [--ovr PATH]
Prints canonical vectors consumed by decomp/tests/test_dispatch.c.
"""
import argparse
import struct
import sys

sys.path.insert(0, ".")
from tools.mips_emu import Emu, Trap
from tools.vol_walk import install_bios, STACK_TOP

DISPATCH = 0x80017784
SYNC0 = 0x8001710C
WAIT0 = 0x80017174
WAIT1 = 0x800171B8
SUB = 0x80017200
POLL = 0x800833E8
WORKER = 0x800472D4
WORKER2 = 0x8004DF34
TABLE1 = 0x8002F0B8
TABLE2 = 0x8002F058
TASK = 0x801C98E0
T2 = TASK + 0xBF7C   # menu-state block: mode+0xA, halves+0x582/584,
                     # writeback+0x58 (game: taskbase+0xBF7C+off)
OBJ = 0x801F4000

ARM_A = 0x800177E8
ARM_B = 0x8001782C
ARM_C = 0x80017860
ARM_D = 0x800178C8


def fresh(scus, ovr):
    emu = Emu(scus, vram_base=0x80010000, file_off=0x800)
    emu.map(0x800A0000, bytearray(0xA8000))
    emu.map(0x801C0000, bytearray(0x20000))
    emu.map(0x801E0000, bytearray(0x20000))
    emu.map(0x801F0000, bytearray(0x10000))
    install_bios(emu)
    emu.write(0x80010000, ovr)
    emu.write(OBJ, bytes(0x800))
    return emu


class Hooks:
    def __init__(self):
        self.calls = []
        self.poll_script = [0]
        self.poll_i = 0

    def install(self, emu):
        def poll(e):
            v = self.poll_script[min(self.poll_i,
                                     len(self.poll_script) - 1)]
            self.poll_i += 1
            self.calls.append(("P", e.regs[4]))
            e.regs[2] = v & 0xFFFFFFFF
            e.pc = e.regs[31]

        def worker(name):
            def h(e):
                self.calls.append((name, e.regs[4], e.regs[5]))
                e.regs[2] = 0
                e.pc = e.regs[31]
            return h

        emu.hooks[POLL] = poll
        emu.hooks[WORKER] = worker("W")
        emu.hooks[WORKER2] = worker("W2")

    def reset(self, emu, poll_script=(0,)):
        self.calls.clear()
        self.poll_script = list(poll_script)
        self.poll_i = 0
        emu.write(0x801FF000, bytes(0x1000))
        emu.write(OBJ, bytes(0x800))


def scrub_task(emu):
    emu.write(TASK, bytes(0xD000))   # covers T2+0x586 too


def set_mode(emu, mode):
    emu.write(T2 + 0xA, bytes([mode & 0xFF]))


def seed_table(emu, base, addrs):
    emu.write(base, struct.pack("<%dI" % len(addrs), *addrs))


def obj_flag(emu, off, val, size=1):
    emu.write(OBJ + off, bytes([val & 0xFF]) if size == 1
              else struct.pack("<h", val))


def show(emu, hk, tag):
    seq = " ".join(n for n, *_ in hk.calls)
    tail = emu.read(T2 + 0x58, 2).hex()
    s5d1 = emu.read(OBJ + 0x5D1, 1)[0]
    print(f"  {tag}: ret=? calls=[{seq}] n={len(hk.calls)} "
          f"task58={tail} s5d1={s5d1:#04x} steps={emu.steps}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scus", default="disc/SCUS_944.88")
    ap.add_argument("--ovr", default="overlays/gt2_01.exe")
    a = ap.parse_args()
    scus = open(a.scus, "rb").read()[0x800:]
    ovr = open(a.ovr, "rb").read()
    emu = fresh(scus, ovr)
    hk = Hooks()
    hk.install(emu)

    print("== guards (no arms) ==")
    for flag5d0, mode, tag in [(0, 1, "flag0"), (1, 0, "mode0"),
                               (1, 11, "mode11"), (1, 5, "mode5/hole")]:
        scrub_task(emu)
        hk.reset(emu)
        obj_flag(emu, 0x5D0, flag5d0)
        set_mode(emu, mode)
        seed_table(emu, TABLE1, [0xBAADF00D] * 10)
        seed_table(emu, TABLE2, [0x80017254] * 11)
        try:
            r = emu.call(DISPATCH, a0=OBJ, sp=STACK_TOP, limit=20000)
            show(emu, hk, f"{tag} ret={r:#x}")
        except Trap as t:
            print(f"  {tag}: TRAP {t} pc={emu.pc:08x} "
                  f"calls={[n for n, *_ in hk.calls]}")

    print("== arms via TABLE1 (mode fixed 3) ==")
    for arm, aname in [(ARM_A, "A"), (ARM_B, "B"), (ARM_C, "C"),
                       (ARM_D, "D")]:
        for fl in (0, 2, 5):
            scrub_task(emu)
            hk.reset(emu)
            obj_flag(emu, 0x5D0, 1)
            set_mode(emu, 3)
            seed_table(emu, TABLE1, [arm] * 10)
            seed_table(emu, TABLE2, [0x80017254] * 11)
            obj_flag(emu, 0x408, fl)
            try:
                r = emu.call(DISPATCH, a0=OBJ, sp=STACK_TOP,
                             limit=50000)
                show(emu, hk, f"arm{aname} fl={fl} ret={r:#x}")
            except Trap as t:
                print(f"  arm{aname} fl={fl}: TRAP {t} "
                      f"pc={emu.pc:08x}")

    print("== tail coupling (arm D, halves) ==")
    for h2, h4, tag in [(-1, -1, "negneg"), (0, 0, "zero"),
                        (-5, -3, "negs"), (5, 5, "pos"), (2, 7, "small")]:
        scrub_task(emu)
        hk.reset(emu)
        obj_flag(emu, 0x5D0, 1)
        set_mode(emu, 3)
        seed_table(emu, TABLE1, [ARM_D] * 10)
        seed_table(emu, TABLE2, [0x80017254] * 11)
        emu.write(T2 + 0x582, struct.pack("<hh", h2, h4))
        # Copy source per the tail index math (taskbase-relative, only
        # when both halves >= 0):
        #   v0 = h2*16424+0x3C74, addr = TASK+v0+h4*164+0xA6
        # Dest is T2+0x58 (NOT taskbase+0x58).
        src = None
        if h2 >= 0 and h4 >= 0:
            v0 = (h2 * 16424 + 0x3C74) & 0xFFFFFFFF
            addr = (TASK + v0 + h4 * 164 + 0xA6) & 0xFFFFFFFF
            if TASK <= addr < TASK + 0xD000:
                src = addr
                emu.write(addr, struct.pack("<H", 0x5EED))
        try:
            r = emu.call(DISPATCH, a0=OBJ, sp=STACK_TOP, limit=50000)
            w = struct.unpack("<H", emu.read(T2 + 0x58, 2))[0]
            show(emu, hk, f"{tag} ret={r:#x} w58={w:#06x} "
                          f"src={src:#x}" if src else f"{tag} ret={r:#x} "
                          f"w58={w:#06x} src=None")
        except Trap as t:
            print(f"  {tag}: TRAP {t} pc={emu.pc:08x}")

    print("== sync_0 (standalone) ==")
    for n5d4, tag in [(0, "n0"), (1, "n1"), (3, "n3")]:
        scrub_task(emu)
        hk.reset(emu)
        emu.write(OBJ, bytes(0x800))
        emu.write(OBJ + 0x5D2, struct.pack("<hhhh", 0x11, n5d4, 0x33,
                                           0x44))
        try:
            emu.call(SYNC0, a0=OBJ, a1=0x77, a2=0x99, sp=STACK_TOP,
                     limit=5000)
            t = emu.read(T2 + 8, 10).hex()
            e8 = emu.read(T2 + 0xE8, 4).hex()
            b8 = emu.read(T2 + 0x1B8, 4).hex()
            c8 = emu.read(T2 + 0x288, 4).hex()
            b5a = emu.read(T2 + 0x5A, 1)[0]
            print(f"  {tag}: task8={t} E8={e8} 1B8={b8} 288={c8} "
                  f"b5a={b5a:#04x} "
                  f"obj5d8={emu.read(OBJ+0x5D8,2).hex()} "
                  f"steps={emu.steps}")
        except Trap as t:
            print(f"  {tag}: TRAP {t}")

    print("== wait0/wait1 (poll scripts) ==")
    for entry, script, tag in [(WAIT0, [1], "w0-immediate"),
                               (WAIT0, [-1, 5, 0], "w0-retry"),
                               (WAIT1, [1], "w1-immediate")]:
        scrub_task(emu)
        hk.reset(emu, poll_script=script)
        try:
            emu.call(entry, a0=OBJ, sp=STACK_TOP, limit=5000)
            print(f"  {tag}: s38b={emu.read(OBJ+0x38B,1)[0]} "
                  f"polls={len(hk.calls)} steps={emu.steps}")
        except Trap as t:
            print(f"  {tag}: TRAP {t}")

    print("== subdispatch (TABLE2 -> fallthrough, flag sweeps) ==")
    for fl, tag in [(0, "fl0"), (1, "fl1")]:
        scrub_task(emu)
        hk.reset(emu)
        obj_flag(emu, 0x5D0, 1)
        set_mode(emu, 1)
        seed_table(emu, TABLE2, [0x80017254] * 11)
        obj_flag(emu, 0x5D1, fl)
        obj_flag(emu, 0x38C, 0, 1)
        try:
            r = emu.call(SUB, a0=OBJ, a1=0x11, a2=0x22, sp=STACK_TOP,
                         limit=50000)
            show(emu, hk, f"{tag} ret={r:#x}")
        except Trap as t:
            print(f"  {tag}: TRAP {t} pc={emu.pc:08x} "
                  f"calls={[n for n, *_ in hk.calls]}")


if __name__ == "__main__":
    main()
