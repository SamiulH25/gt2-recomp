#!/usr/bin/env python3
"""Kill-switch experiment for the SCUS frustum trivial-reject test.

Inserts an env-gated early return (v0=0, "nothing culled") on the
fall-through path of generated func_8007B640 (fresh entries only; CPS
resume gotos jump past it). Defined in src/mods/gt2_batch_jobs.c.

Enable with PSX_NO_FRUSTUM_CULL=1. If edge pop-in vanishes, the outcode
test is the popper and the real fix is widening its 4:3 bounds for 16:9
— not disabling it (uncapped overdraw).

Idempotent; re-run after regen; fails loudly on drift.

Usage: python3 tools/patch_scus_nocull.py [--check]
"""
import pathlib
import sys

PATH = pathlib.Path(__file__).resolve().parent.parent / \
    "generated" / "SCUS_944.88_full_16.c"

ANCHOR = "    debug_server_log_call_entry(0x8007B640u);"
HOOK = ("    { extern int gt2_nocull_try(CPUState *cpu);\n"
        "      if (gt2_nocull_try(cpu)) return; }\n")


def main() -> int:
    check_only = "--check" in sys.argv
    src = PATH.read_text()
    if src.count(ANCHOR) != 1:
        print(f"ANCHOR NOT UNIQUE ({src.count(ANCHOR)}x)")
        return 1
    if "gt2_nocull_try(cpu)" in src:
        print("hook already present")
        return 0
    if check_only:
        print("would insert hook")
        return 0
    src = src.replace(ANCHOR, HOOK + ANCHOR, 1)
    PATH.write_text(src)
    print("hook inserted")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
