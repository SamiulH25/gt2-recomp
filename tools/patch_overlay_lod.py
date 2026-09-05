#!/usr/bin/env python3
"""GT2 LOD / render-distance tuning for the statically recompiled overlays.

Patches generated/overlays/overlays_static.c (checked in; regen via
psxrecomp/tools/compile_overlays.py would overwrite, so re-run this script
after any regen). All edits are anchored on the recompiler's per-PC comments
(/* 0x8002xxxx: 0x........ */) and assert uniqueness before replacing.

Knobs (decoded from overlays/gt2_01.exe, Sept 2026):
- Far-plane gate x2: funcB 0x80020110 phase-1 loop culls objects whose fast
  approx distance (helper 0x80020FD8: max+med/2+min/4) exceeds fp=0x63FFFF
  (lui $fp,0x63 @0x8002024C + ori 0xFFFF @0x80020274, sltu @0x800202E4).
  Same constant gates a second object class in func_800140A4
  (lui $v0,0x63 @0x800141C4 + ori @0x800141C8, slt @0x800141CC).
  0x63 -> 0xC6 doubles both to ~0xC6FFFF (~13.1M units).
- LOD switch 3 -> 6: node field 0xC < N selects the near (inline-RTPS
  billboard) path over the far funnel pair (sltiu @0x80020428).
  Higher N keeps near detail further out at higher GPU cost.

Usage: python3 tools/patch_overlay_lod.py [--check]
"""
import pathlib
import sys

PATH = pathlib.Path(__file__).resolve().parent.parent / \
    "generated" / "overlays" / "overlays_static.c"

EDITS = [
    # (anchor substring, old, new) -- anchor must occur exactly once
    ("0x8002024C: 0x3C1E0063",
     "cpu->gpr[30] = 0x0063 << 16;  /* 0x00630000 */",
     "cpu->gpr[30] = 0x00C6 << 16;  /* 0x00C60000 (LOD patch: 2x far plane) */"),
    ("0x8002024C: 0x3C1E0063",
     "PGXP_ALU(0x3C1E0063u,",
     "PGXP_ALU(0x3C1E00C6u,"),
    ("0x800141C4: 0x3C020063",
     "cpu->gpr[2] = 0x0063 << 16;  /* 0x00630000 */",
     "cpu->gpr[2] = 0x00C6 << 16;  /* 0x00C60000 (LOD patch: 2x far plane) */"),
    ("0x800141C4: 0x3C020063",
     "PGXP_ALU(0x3C020063u,",
     "PGXP_ALU(0x3C0200C6u,"),
    ("0x80020428: 0x2E220003",
     "cpu->gpr[2] = (cpu->gpr[17] < (uint32_t)3) ? 1 : 0;  /* 0x80020428: 0x2E220003 */",
     "cpu->gpr[2] = (cpu->gpr[17] < (uint32_t)6) ? 1 : 0;  /* 0x80020428: 0x2E220006 (LOD patch: near path <6) */"),
]


def main() -> int:
    check_only = "--check" in sys.argv
    src = PATH.read_text()
    changed = 0
    for anchor, old, new in EDITS:
        if src.count(anchor) < 1:
            print(f"ANCHOR MISSING: {anchor}")
            return 1
        if src.count(old) != 1:
            print(f"NOT UNIQUE ({src.count(old)}x): {old[:70]}")
            return 1
        if new in src:
            print(f"already applied: {anchor}")
            continue
        if check_only:
            print(f"would apply: {anchor}")
            continue
        src = src.replace(old, new)
        changed += 1
        print(f"patched: {anchor}")
    if not check_only and changed:
        PATH.write_text(src)
    print(f"{'would change' if check_only else 'changed'} {changed}/{len(EDITS)} sites")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
