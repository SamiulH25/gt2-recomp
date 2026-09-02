#!/usr/bin/env python3
"""Probe GT2 disc for psxrecomp game.toml seeds using gt2-reversing symbols"""
import pathlib, hashlib, struct

ISO = pathlib.Path("/tmp/opencode/gt2.iso")
SCUS = pathlib.Path("/tmp/opencode/SCUS_944.88")

# Generate first-pass seeds: entry PC + JAL targets from SCUS text
def jal_seeds(exe_path):
    data = open(exe_path,'rb').read()
    code = data[0x800:]
    base = 0x80010000
    seeds = set()
    seeds.add(0x8005D600) # PC from header
    for i in range(0, len(code)-4, 4):
        w = struct.unpack_from('<I', code, i)[0]
        if (w >> 26) == 3: # JAL
            target = (w & 0x3FFFFFF) << 2
            dest = (base & 0xF0000000) | target
            # Only add if dest within exe range
            if 0x80010000 <= dest < 0x80010000 + len(code):
                seeds.add(dest)
    return sorted(seeds)

seeds = jal_seeds(SCUS)
print(f"found {len(seeds)} seeds from SCUS (jal targets + entry)")
print(seeds[:20])
# Also merge gt2-reversing mainexe symbols for oracle seeds
try:
    sym_file = pathlib.Path("_upstream/gt2-reversing/config/gt2_us12_simdisk/mainexe_symbol_addrs.txt")
    if sym_file.exists():
        rev_seeds=[]
        for line in open(sym_file):
            if "=" in line:
                addr = line.split("=")[1].strip().strip(";")
                try:
                    v=int(addr,16)
                    rev_seeds.append(v)
                except: pass
        print(f"gt2-reversing mainexe symbols {len(rev_seeds)}")
        merged = sorted(set(seeds) | set(rev_seeds))
        print(f"merged {len(merged)} seeds")
        pathlib.Path("seeds").mkdir(exist_ok=True)
        open("seeds/ghidra_funcs.txt","w").write("\n".join(f"{a:08x}" for a in merged))
        print("wrote seeds/ghidra_funcs.txt")
except Exception as e:
    print(e)

# Probe disc identity (for game.toml)
# Read SYSTEM.CNF BOOT line already known: SCUS_944.88
# Compute cue sha? Just use bin sha1
import hashlib
h=hashlib.sha256()
h.update(open(ISO,'rb').read(1024*1024))
print(f"iso head sha256 {h.hexdigest()[:16]}... (full file large, not computed)")
# Use cue
cue = pathlib.Path("Gran Turismo 2 (USA) (Simulation Mode) (v1.2)/Gran Turismo 2 (USA) (Simulation Mode) (v1.2).cue")
if cue.exists():
    print(open(cue).read())
