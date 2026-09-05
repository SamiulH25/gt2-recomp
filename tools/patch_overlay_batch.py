#!/usr/bin/env python3
"""Hook GT2's billboard parent for P2 parallel batch dispatch.

Inserts a delegation hook at the fresh-entry point of
ov_00010000_74BDB962_F608722F_func_8002002C (after the CPS resume switch,
before the prologue): when src/mods/gt2_batch_jobs.c accepts the call it
runs the 33 funcA batches on the worker pool and returns 1 (parent
returns immediately, guest callee-saved regs untouched); otherwise the
original serial body runs bit-exact.

Idempotent (skips when the hook is present) and anchored on unique
generated lines. Re-run after any overlay regen; fails loudly if the
namespace or anchor moved.

Usage: python3 tools/patch_overlay_batch.py [--check]
"""
import pathlib
import sys

PATH = pathlib.Path(__file__).resolve().parent.parent / \
    "generated" / "overlays" / "overlays_static.c"

NS = "ov_00010000_74BDB962_F608722F"
PARENT = f"{NS}_func_8002002C"

ANCHOR_DECL = f"void {PARENT}(CPUState* cpu);"
ANCHOR_CASE = "            case 0x8002002Cu: break;  /* entry at prologue */"
ANCHOR_CALL = "    debug_server_log_call_entry(0x8002002Cu);"
# Fresh dispatch entry (pc == entry): offer to the batch runner first.
HOOK_CASE = ("            case 0x8002002Cu: { extern int gt2_batch_parent_try"
             "(CPUState *cpu);\n"
             "                if (gt2_batch_parent_try(cpu)) return; break; }\n")
# Direct C entry (pc == 0, switch skipped): same offer. Resume entries
# goto their blocks below and never reach this line.
HOOK_CALL = ("    { extern int gt2_batch_parent_try(CPUState *cpu);\n"
             "      if (cpu->pc == 0u && gt2_batch_parent_try(cpu)) return; }\n")


def main() -> int:
    check_only = "--check" in sys.argv
    src = PATH.read_text()
    if src.count(ANCHOR_DECL) < 1:
        print(f"NAMESPACE MOVED (regen?): {ANCHOR_DECL}")
        return 1
    if src.count(ANCHOR_CASE) != 1 or src.count(ANCHOR_CALL) != 1:
        print("ANCHOR NOT UNIQUE")
        return 1
    if "gt2_batch_parent_try(cpu)" in src:
        print("hook already present")
        return 0
    if check_only:
        print("would insert hook (2 sites)")
        return 0
    src = src.replace(ANCHOR_CASE, HOOK_CASE, 1)
    src = src.replace(ANCHOR_CALL, HOOK_CALL + ANCHOR_CALL, 1)
    PATH.write_text(src)
    print("hook inserted (2 sites)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
