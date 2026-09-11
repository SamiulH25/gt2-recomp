#!/usr/bin/env python3
"""Overlay member census (Phase D.1): roles for gt2_02..06 and re-baseline.

Per member of overlays/gt2_0{1..6}.exe (VRAM 0x80010000):
- size, entry prologue (first 3 words), GTE breakdown (COP2 total, RTPS
  0x4A180001, RTPT 0x4A280030), syscall count, JAL/JR counts, lui/anchor
  pairs (lui $at,0x1F80 + sw __,0x70($at)), slti-0x140 screen-cull hits,
  printable strings (>=6 chars, capped), splat function-name leads from
  _upstream/gt2-reversing (cited as leads, never sources).

Usage: python3 tools/ovl_census.py [--md]  # --md emits markdown table
"""
import os
import re
import struct
import sys

OVL_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "overlays")
SPLAT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "..", "_upstream", "gt2-reversing",
                         "config", "gt2_us12_simdisk")
VRAM = 0x80010000


def words(d):
    n = len(d) // 4
    return list(struct.unpack(f"<{n}I", d[:n * 4]))


def census(path):
    d = open(path, "rb").read()
    w = words(d)
    n = len(w)
    cop2 = sum(1 for x in w if x >> 26 == 0x12)
    rtps = sum(1 for x in w if x == 0x4A180001)
    rtpt = sum(1 for x in w if x == 0x4A280030)
    syscall = sum(1 for x in w if x == 0x0000000C)
    jal = sum(1 for x in w if x >> 26 == 0x03)
    jr = sum(1 for x in w if (x >> 26 == 0) and (x & 0x3F) == 0x08)
    # lui $at,0x1F80 (0x3C011F80) followed within 8 insns by sw *,0x70($at?)
    # ($at = reg 1: sw opcode 0x2B, base 1, off 0x70 -> 0xAC810070 masked)
    anchors = 0
    for i, x in enumerate(w):
        if x == 0x3C011F80:
            for y in w[i + 1:i + 9]:
                if (y >> 26) == 0x2B and ((y >> 21) & 31) == 1 and \
                        (y & 0xFFFF) == 0x70:
                    anchors += 1
                    break
    # slti/sltiu with width-ish immediates (screen-cull shape)
    cull = sum(1 for x in w if (x >> 26) in (0x0A, 0x0B) and
               (x & 0xFFFF) in (0x140, 0x141, 0xE0, 0xF0, 0xF1))
    strings = []
    for m in re.finditer(rb"[ -~]{6,}", d):
        s = m.group().decode()
        if len(strings) < 60:
            strings.append((m.start(), s))
    return {
        "size": len(d), "prologue": [f"{x:08x}" for x in w[:3]],
        "cop2": cop2, "rtps": rtps, "rtpt": rtpt, "syscall": syscall,
        "jal": jal, "jr": jr, "anchors": anchors, "cull": cull,
        "strings": strings,
    }


def splat_leads(member):
    """Function-name leads from the splat yaml (names only)."""
    p = os.path.join(SPLAT_DIR, f"{member}.yaml")
    try:
        txt = open(p).read()
    except OSError:
        return []
    names = re.findall(r"ovr\d+/([A-Za-z0-9_]+)", txt)
    seen, out = set(), []
    for x in names:
        if x not in seen:
            seen.add(x)
            out.append(x)
    return out


def main():
    md = "--md" in sys.argv
    rows = []
    for i in range(1, 7):
        name = f"gt2_0{i}"
        c = census(os.path.join(OVL_DIR, name + ".exe"))
        c["name"] = name
        c["leads"] = splat_leads(name)
        rows.append(c)
    if md:
        print("| member | size | COP2 | RTPS | RTPT | syscall | JAL | JR | "
              "anchor-pairs | cull-hits |")
        print("|---|---|---|---|---|---|---|---|---|---|---|")
        for c in rows:
            print(f"| {c['name']} | {c['size']} | {c['cop2']} | {c['rtps']} "
                  f"| {c['rtpt']} | {c['syscall']} | {c['jal']} | {c['jr']} "
                  f"| {c['anchors']} | {c['cull']} |")
    for c in rows:
        print(f"=== {c['name']} ({c['size']} B) ===")
        print(f"  prologue: {' '.join(c['prologue'])}")
        print(f"  COP2={c['cop2']} RTPS={c['rtps']} RTPT={c['rtpt']} "
              f"syscall={c['syscall']} JAL={c['jal']} JR={c['jr']} "
              f"anchors={c['anchors']} cull={c['cull']}")
        print(f"  splat leads ({len(c['leads'])}): "
              f"{', '.join(c['leads'][:12])}")
        interesting = [s for _, s in c["strings"]
                       if not s.startswith(("AAAAAAAA", "        "))]
        print(f"  strings ({len(c['strings'])} total):")
        for off, s in c["strings"][:25]:
            print(f"    @{off:#x}: {s[:72]}")


if __name__ == "__main__":
    main()
