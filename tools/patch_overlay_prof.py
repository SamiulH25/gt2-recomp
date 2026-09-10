#!/usr/bin/env python3
"""CPS-safe funcB profiler counters (profile-first threading phase).

Inserts gt2_prof_note_*() calls (defined in src/mods/gt2_batch_jobs.c):
  3 funcB sites in the STATIC funcB (ov_00010000_74BDB962_F608722F_func_80020110,
  Size 3368 — NOT the same-VRAM 136-byte member in ov_00010000_1097C91D):
  entry: funcB fresh-entry point (after the Size-3368 Address comment;
    resume continuations goto their blocks below and never reach it)
  near:  LOD branch _bc_8002042C not-taken -> block_80020434 (inline RTPS)
  far:   LOD branch _bc_8002042C taken -> block_80020A00 (funnel pair)
  2 hit sites in psx_overlay_dispatch() (memo + variant-loop paths):
  per-hit addr sampling into a mod-side histogram (top-N in sampler dump)

Counters only — no control flow changes, so none of the parked-P2 CPS
hazards apply. Output only when PSX_PROFLOG=1 (run with PSX_GATELOG=1 for
objs/call). Idempotent; re-run after regen; fails loudly on drift.

Usage: python3 tools/patch_overlay_prof.py [--check]
"""
import pathlib
import sys

PATH = pathlib.Path(__file__).resolve().parent.parent / \
    "generated" / "overlays" / "overlays_static.c"

ENTRY_ANCHOR = "    /* Address: 0x80020110, Size: 3368 bytes, Blocks: 65 */"
ENTRY_ADD = ("\n    { extern void gt2_prof_note_entry(void); "
             "gt2_prof_note_entry(); }")

NEAR_ANCHOR = ("        psx_check_interrupts_at(cpu, 0x80020434u);\n"
               "        goto block_80020434;  /* not taken */")
NEAR_ADD = ("        { extern void gt2_prof_note_near(void); "
            "gt2_prof_note_near(); }\n")

FAR_ANCHOR = ("        psx_check_interrupts_at(cpu, 0x80020A00u);\n"
              "        goto block_80020A00;  /* taken */")
FAR_ADD = ("        { extern void gt2_prof_note_far(void); "
           "gt2_prof_note_far(); }\n")

# Static-dispatch hit sites in psx_overlay_dispatch(): after the CRC gate
# passes, before v->fn(cpu). `addr` is the dispatch target. Memo path has
# no last_hit store; loop path does — that disambiguates the anchors.
HIT_PRE = ("                { extern void gt2_prof_note_static_hit(uint32_t); "
           "gt2_prof_note_static_hit(addr); }\n")
HIT_MEMO_ANCHOR = ("                psx_ov_static_hits++;\n"
                   "                v->fn(cpu);\n"
                   "                return 1;")
HIT_LOOP_ANCHOR = ("                psx_ov_static_hits++;\n"
                   "                psx_ov_last_hit[ei] = (uint16_t)i;\n"
                   "                v->fn(cpu);\n"
                   "                return 1;")

SITES = (
    (ENTRY_ANCHOR, ENTRY_ADD, "entry", "after"),
    (NEAR_ANCHOR, NEAR_ADD, "near", "before"),
    (FAR_ANCHOR, FAR_ADD, "far", "before"),
)


HIT_SITES = (
    (HIT_MEMO_ANCHOR, "hit-memo"),
    (HIT_LOOP_ANCHOR, "hit-loop"),
)

# Dispatch-miss sites in psx_overlay_dispatch() (deep-log flight recorder):
# - MISS: address compiled somewhere, but no occupant resident (CRC/band)
#   -> falls through to the interpreter.Answers "what did the game call
#   that static dispatch couldn't serve".
# - AMISS: hash slot empty (compiled nowhere). Gated in-macro on the
#   deeplog flag AND overlay range inside the note fn; the hot path pays
#   one predictable branch + (usually) one call it would make anyway.
MISS_ANCHOR = ("    /* Address is ours but no occupant is resident -> interpreter. */\n"
               "    return 0;")
MISS_ADD = ("    { extern int g_gt2_deeplog; extern void gt2_prof_note_dispatch_miss(uint32_t);\n"
            "      if (g_gt2_deeplog) gt2_prof_note_dispatch_miss(addr); }\n")
AMISS_ANCHOR = ("            psx_ov_static_address_misses++;\n"
                "            return 0;")
AMISS_ADD = ("            { extern int g_gt2_deeplog; extern void gt2_prof_note_address_miss(uint32_t);\n"
             "              if (g_gt2_deeplog) gt2_prof_note_address_miss(addr); }\n")

MISS_SITES = (
    (MISS_ANCHOR, MISS_ADD, "miss", "before"),
    (AMISS_ANCHOR, AMISS_ADD, "amiss", "before"),
)


def main() -> int:
    check_only = "--check" in sys.argv
    src = PATH.read_text()
    have_core = "gt2_prof_note_entry" in src and \
        "gt2_prof_note_near" in src and "gt2_prof_note_far" in src
    have_hit = "gt2_prof_note_static_hit" in src
    have_miss = "gt2_prof_note_dispatch_miss" in src and \
        "gt2_prof_note_address_miss" in src
    if have_core and have_hit and have_miss:
        print("profiler already present (3 funcB + 2 hit + 2 miss sites)")
        return 0
    if "gt2_prof_note_" in src and not have_core:
        print("PARTIAL profiler present — regen or revert before re-applying")
        return 1
    if not have_core:
        for anchor, _add, name, _pos in SITES:
            if src.count(anchor) != 1:
                print(f"ANCHOR NOT UNIQUE ({src.count(anchor)}x): {name}")
                return 1
    if not have_hit:
        for anchor, name in HIT_SITES:
            if src.count(anchor) != 1:
                print(f"ANCHOR NOT UNIQUE ({src.count(anchor)}x): {name}")
                return 1
    if not have_miss:
        for anchor, _add, name, _pos in MISS_SITES:
            if src.count(anchor) != 1:
                print(f"ANCHOR NOT UNIQUE ({src.count(anchor)}x): {name}")
                return 1
    if check_only:
        print("would insert profiler (%d sites)" %
              ((0 if have_core else 3) + (0 if have_hit else 2) +
               (0 if have_miss else 2)))
        return 0
    if not have_core:
        for anchor, add, _name, pos in SITES:
            if pos == "after":
                src = src.replace(anchor, anchor + add, 1)
            else:
                src = src.replace(anchor, add + anchor, 1)
    if not have_hit:
        for anchor, _name in HIT_SITES:
            # Insert before v->fn(cpu); keep indentation (16 spaces).
            old = anchor.split("\n")
            new = "\n".join(old[:-2] + [HIT_PRE + old[-2], old[-1]])
            src = src.replace(anchor, new, 1)
    if not have_miss:
        for anchor, add, _name, pos in MISS_SITES:
            if pos == "after":
                src = src.replace(anchor, anchor + add, 1)
            else:
                src = src.replace(anchor, add + anchor, 1)
    PATH.write_text(src)
    print("profiler inserted")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
