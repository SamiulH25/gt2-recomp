#!/usr/bin/env python3
"""Meter funcB's per-object cull gates (binding-constraint probe).

Inserts gt2_gate_note() calls (defined in src/mods/gt2_batch_jobs.c) at
the four outcomes of the phase-1 loop in generated func_80020110:
  kind 0 (bypass): _bc_800202E0 taken  -> block_800202F0 (s6==0, no gate)
  kind 1 (reject): _bc_800202E8 taken  -> block_80020398 (dist > threshold)
  kind 2 (class):  _bc_80020328 taken  -> block_80020398 (helper says skip)
  kind 3 (accept): _bc_800202E8 fallthrough -> block_800202F0

Counting is unconditional (one call per object); printing only when
PSX_GATELOG=1. Idempotent; re-run after regen; fails loudly on drift.

Usage: python3 tools/patch_overlay_gatemeter.py [--check]
"""
import pathlib
import sys

PATH = pathlib.Path(__file__).resolve().parent.parent / \
    "generated" / "overlays" / "overlays_static.c"

NOTE = "{ extern void gt2_gate_note(int); gt2_gate_note(%d); }"

EDITS = [
    # (unique anchor, kind) — anchor line is kept, note inserted before goto
    ("        goto block_800202F0;  /* taken */", 0),
    ("    if (_bc_800202E8) {\n"
     "#ifdef PSX_ENABLE_BLOCK_CYCLES\n"
     "        psx_cyc_bb_defer_flush();\n"
     "#endif\n"
     "        psx_check_interrupts_at(cpu, 0x80020398u);\n"
     "        goto block_80020398;  /* taken */", 1),
    ("    if (_bc_80020328) {\n"
     "#ifdef PSX_ENABLE_BLOCK_CYCLES\n"
     "        psx_cyc_bb_defer_flush();\n"
     "#endif\n"
     "        psx_check_interrupts_at(cpu, 0x80020398u);\n"
     "        goto block_80020398;  /* taken */", 2),
    ("        goto block_800202F0;  /* not taken */", 3),
]

# Outcode sampling: v1 (gpr[3]) holds the 0x8007B640 return at both
# post-class branches. Applied after EDITS (anchors on inserted lines).
CODE_EDITS = [
    ("        { extern void gt2_gate_note(int); gt2_gate_note(2); }\n"
     "        goto block_80020398;  /* taken */",
     "        { extern void gt2_gate_note(int); gt2_gate_note(2); }\n"
     "        { extern void gt2_gate_code(uint32_t,int); gt2_gate_code(cpu->gpr[3],1); }\n"
     "        goto block_80020398;  /* taken */"),
    ("        goto block_80020330;  /* not taken */",
     "        { extern void gt2_gate_code(uint32_t,int); gt2_gate_code(cpu->gpr[3],0); }\n"
     "        goto block_80020330;  /* not taken */"),
]


def main() -> int:
    check_only = "--check" in sys.argv
    src = PATH.read_text()
    if "gt2_gate_note(" not in src:
        for anchor, kind in EDITS:
            if src.count(anchor) != 1:
                print(f"ANCHOR NOT UNIQUE ({src.count(anchor)}x): {anchor[:60]!r}")
                return 1
        if check_only:
            print("would insert meter (4 sites)")
            return 0
        for anchor, kind in EDITS:
            lines = anchor.split("\n")
            replacement = "\n".join(lines[:-1] + ["        " + NOTE % kind,
                                                  lines[-1]])
            src = src.replace(anchor, replacement, 1)
    elif not check_only:
        print("meter already present")
    if "gt2_gate_code(" in src:
        print("code samplers already present")
        PATH.write_text(src)
        return 0
    if check_only:
        print("would insert code samplers (2 sites)")
        return 0
    for anchor, replacement in CODE_EDITS:
        if src.count(anchor) != 1:
            print(f"CODE ANCHOR NOT UNIQUE ({src.count(anchor)}x)")
            return 1
        src = src.replace(anchor, replacement, 1)
    PATH.write_text(src)
    print("meter inserted (4 sites + 2 code samplers)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
