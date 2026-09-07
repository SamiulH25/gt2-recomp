#!/usr/bin/env python3
"""Emulate SCUS vol functions against a modeled RAM image.

Replicates the Q3 init memory effects in Python (CD reads can't run under
the emulator), then executes the REAL search_vol_dir / build_cache code:
  - vol_buffer 0x800A97D0 <- VOL[0 : 0x8C000)
  - header copy 0x801E35F0 <- first 0xC000 of vol_buffer
  - slide: vol_buffer[0 : 0xC000] <- VOL[0xB800 : 0xB800+0xC000)
Then calls 0x800100E4(path, 0) and 0x80010228() and reports.

Usage: tools/vol_walk.py [--iso PATH] [--one ADDR] [--trace]
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, ".")
from tools.mips_emu import Emu, Trap

SCUS_VRAM = 0x80010000
SCUS_OFF = 0x800
VOL_BUF = 0x800A97D0
VOL_READ = 0x8C000
HDR_COPY = 0x801E35F0
HDR_SIZE = 0xC000
CACHE = 0x801E2EF0
STACK_TOP = 0x801FFF00


def load_iso(path):
    with open(path, "rb") as f:
        return f.read()


def install_bios(emu, log=False):
    """Stub the PSX BIOS A-table at 0xA0 (game reaches it via jr $t2)."""
    calls = []

    def a0_table(e):
        fn = e.regs[9]  # $t1 = function number
        a0, a1, a2 = e.regs[4], e.regs[5], e.regs[6]
        calls.append((fn, a0, a1, a2))
        if log and len(calls) < 10:
            print(f"  bios A({fn:#x}) a0={a0:#x} a1={a1:#x} a2={a2:#x}")
        if fn == 0x27:      # bcopy: observed (src=a0, dst=a1, len=a2)
            e.write(a1, e.read(a0, a2))
            e.regs[2] = a1
        elif fn == 0x18:    # memcmp(a0, a1, len=a2): 0 on equal
            a = e.read(a0, a2)
            b = e.read(a1, a2)
            v = 0
            for x, y in zip(a, b):
                if x != y:
                    v = x - y
                    break
            e.regs[2] = v & 0xFFFFFFFF
        else:
            raise Trap(f"unstubbed BIOS A({fn:#x}) a0={a0:#x} a1={a1:#x} a2={a2:#x}")
        e.pc = e.regs[31]   # return (BIOS thunks use jr $t2; ra intact)

    emu.hooks = {0xA0: a0_table}
    return calls


def build_emu(scus_path, iso_bytes, slide=True):
    scus = open(scus_path, "rb").read()[SCUS_OFF:]
    emu = Emu(scus, vram_base=SCUS_VRAM, file_off=SCUS_OFF)
    emu.map(0x800A0000, bytearray(0xA0000))      # vol_buffer + scratch
    emu.map(0x801E0000, bytearray(0x20000))      # header/cache/stack page
    base = 473 * 2048
    vol = iso_bytes[base:base + VOL_READ]
    assert len(vol) == VOL_READ, "short VOL read"
    emu.write(VOL_BUF, vol)
    emu.write(HDR_COPY, vol[:HDR_SIZE])
    if slide:
        emu.write(VOL_BUF, iso_bytes[base + 0xB800:base + 0xB800 + HDR_SIZE])
    return emu


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=os.environ.get("GT2_ISO", "/tmp/opencode/gt2.iso"))
    ap.add_argument("--scus", default="disc/SCUS_944.88")
    ap.add_argument("--one", default=None, help="one path string to resolve")
    ap.add_argument("--addr", default=None, help="VRAM addr of path (default: find --one in SCUS rodata)")
    ap.add_argument("--trace", action="store_true")
    ap.add_argument("--no-slide", action="store_true")
    ap.add_argument("--all", action="store_true", help="resolve all 248 Q2 paths")
    a = ap.parse_args()

    iso = load_iso(a.iso)
    emu = build_emu(a.scus, iso, slide=not a.no_slide)
    bios_calls = install_bios(emu, log=a.trace)

    def resolve(path_addr):
        emu.trace = [] if a.trace else None
        try:
            ret = emu.call(0x800100E4, a0=path_addr, a1=0, sp=STACK_TOP)
        except Trap as t:
            return f"TRAP {t}", None
        return f"{ret:#x}", ret

    if a.one and not a.addr:
        scus = open(a.scus, "rb").read()
        needle = a.one.encode() + b"\x00"
        off = scus.find(needle)
        if off < 0:
            sys.exit(f"path {a.one!r} not in SCUS")
        a.addr = 0x80010000 + (off - SCUS_OFF)
        print(f"path {a.one!r} at {a.addr:#x}")

    if a.addr or a.one:
        addr = int(a.addr, 0) if isinstance(a.addr, str) else a.addr
        how, ret = resolve(addr)
        print(f"search -> {how} steps={emu.steps}")
        if ret:
            raw = emu.read(ret, 32)
            date, nxt = struct.unpack_from("<IH", raw, 0)
            print(f"entry: date={date:08x} next={nxt} flags={raw[6]:02x} name={raw[7:32]!r}")
        if a.trace:
            for pc, m, op in emu.trace[-25:]:
                print(f"  {pc:08x}: {m:10} {op}")
        return

    # default: emulate build_cache, then dump the cache
    emu.trace = [] if a.trace else None
    try:
        emu.call(0x80010228, sp=STACK_TOP)
    except Trap as t:
        sys.exit(f"build_cache TRAP after {emu.steps} steps: {t}")
    print(f"build_cache ok steps={emu.steps}")
    n = ok = 0
    misses = []
    for i in range(248):
        v = struct.unpack("<H", emu.read(CACHE + i * 2, 2))[0]
        if v != 0xFFFF:
            ok += 1
        else:
            misses.append(i)
        n += 1
    print(f"cache: {ok}/{n} resolved, misses={misses[:10]}")
    if a.all:
        scus = open(a.scus, "rb").read()
        base = 0x800 + (0x8009118C - 0x80010000)
        for i in range(248):
            p = struct.unpack_from("<I", scus, base + i * 4)[0]
            if p == 0:
                break
            s = scus[0x800 + p - 0x80010000:][:48].split(b"\x00")[0].decode()
            v = struct.unpack("<H", emu.read(CACHE + i * 2, 2))[0]
            print(f"  [{i:3}] cache={v:5} {s}")


if __name__ == "__main__":
    main()
