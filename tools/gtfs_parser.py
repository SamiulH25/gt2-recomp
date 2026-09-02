#!/usr/bin/env python3
"""Parse GT2.VOL GTFS - Polyphony GT File System"""
import struct, pathlib
ISO = pathlib.Path("/tmp/opencode/gt2.iso")
# GTFS header at LBA 473
with open(ISO,'rb') as f:
    f.seek(473*2048)
    hdr = f.read(0x2000)
magic, u1, cnt = struct.unpack_from('<4sII', hdr, 0)
# Guess: 0x08 = file count? Let's try
print(f"magic={magic} u1={u1} cnt_candidate={cnt}")
# Dump full header as file table
# From earlier: entry 0 off=0x2fc size=0xbb80 etc
# Try count = value at 0x0C (0x2d642d3d?) no. Another candidate at 0x10 = 0x2fc
# Let's brute: GTFS typically: magic 4, count 4, then entries [offset, size, ???]
# At 0x08 we had 0x2d642d3d = 760M nonsense
# At 0x10 we had 0x2fc = 764 -> plausible header size
# At 0x04 we had 0 -> maybe count is at 0x10+??
# Let's try to parse as: header size 0x800, then table of 8-byte entries until terminator
# List all u32 and try to find monotonic offsets

vals = struct.unpack('<512I', hdr[:2048])
print("first 32 u32:")
for i in range(32):
    print(f"{i*4:03x}: {vals[i]:08x} {vals[i]:10}")

# Attempt: entries start at 0x10, each 8 bytes: offset, size
# Check monotonic: offsets should increase and offset+size ~ next offset
for i in range(20):
    off = vals[4+i*2] # starting at index 4 = 0x10
    sz = vals[5+i*2]
    print(f"entry {i:2}: off={off:08x} ({off:10}) sz={sz:08x} ({sz:10}) next_off_guess={off+sz:08x} valid={off<500_000_000 and sz<10_000_000}")

# File count guess: look for where entries stop being monotonic
# Earlier dump after ~100 entries started to look like 0xECxxxxxx -> likely packed flags not offsets
# Count may be at 0x08? Let's try 0x2fc? No
# Alternative: GT2 known tool: gt2vol extractor shows ~ 4000 files in VOL?
# Check second header area at 0x800?
hdr2 = hdr[0x800:0x1000]
print("\n at 0x800:")
for i in range(16):
    v = struct.unpack_from('<I', hdr, 0x800+i*4)[0]
    print(f"{0x800+i*4:04x}: {v:08x}")

# Try to use known gt2 vol tool logic: GTFS has 0xC header: magic, version, fileCount, headerSize?
# Let's search for fileCount candidate near 3000-6000
for idx, v in enumerate(vals):
    if 1000 < v < 10000:
        # check if next values look like offsets
        nxt = vals[idx+1] if idx+1 < len(vals) else 0
        nxt2 = vals[idx+2] if idx+2 < len(vals) else 0
        if 0x200 < nxt < 500_000_000 and nxt2 < 5_000_000:
            print(f"count candidate at {idx*4:x}: {v} -> next {nxt:08x} {nxt2:08x}")
