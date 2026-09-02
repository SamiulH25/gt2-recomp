# @category PSX
# GT2 Ghidra overlay loader - requires ghidra_psx_ldr
# Based on silent-hill-decomp LoadSHOverlays.py pattern
# Loads SCUS_944.88 + 6 overlays from GT2.OVL decompressed members
# Overlays all map to 0x80010000 (PSX RAM) as per gt2-reversing splat configs
# Run after importing SCUS_944.88 via PS-X EXE loader (choose No at analyze prompt)

import os

# Config: overlay VRAMs from gt2-reversing (all overlays share 0x80010000)
# Decompressed OVL is 1,133,632 bytes containing 6 concatenated gzip members:
# gt2_01..gt2_06, each with its own splat config vram 0x80010000
OVERLAYS = [
    ("gt2_01", 0x80010000, "GT2.OVL.decompressed slice 0: ~316k"),
    ("gt2_02", 0x80010000, "slice 1: ~248k"),
    ("gt2_03", 0x80010000, "slice 2: ~275k"),
    ("gt2_04", 0x80010000, "slice 3: ~11k"),
    ("gt2_05", 0x80010000, "slice 4: ~273k"),
    ("gt2_06", 0x80010000, "slice 5: ~8k"),
]

# Symbol files from ginryuoku/gt2-reversing config/gt2_us12_simdisk
SYMBOL_FILES = [
    "mainexe_symbol_addrs.txt",
    "ovr1_symbol_addrs.txt",
    "ovr2_symbol_addrs.txt",
    "ovr3_symbol_addrs.txt",
    "ovr4_symbol_addrs.txt",
    "ovr5_symbol_addrs.txt",
    "ovr6_symbol_addrs.txt",
]

def run():
    # This is a stub that documents manual steps; actual Ghidra Jython will replace with API calls
    # Manual workflow (ghidra_psx_ldr installed):
    # 1. Ghidra -> File -> New Project -> Non-Shared
    # 2. Import File -> SCUS_944.88 (Loader: PS-X EXE, Language: MIPS:LE:32:default/PSX)
    # 3. When asked Analyze -> No
    # 4. File -> Add to Program -> select GT2.OVL.decompressed slice files -> check Overlay, name gt2_01 etc, base 0x80010000, check R,X,Overlay,Initialized
    # 5. Window -> Script Manager -> GT2 -> LoadGT2Overlays.py -> Run
    # 6. Script will create overlay blocks and apply symbols from mainexe_symbol_addrs.txt (format: name = 0x80010000;)
    print("GT2 overlays: all 6 at 0x80010000 as per splat")
    for name, vram, note in OVERLAYS:
        print(f"  {name} @ {hex(vram)} ({note})")

if __name__ == "__main__":
    run()
