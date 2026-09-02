#!/usr/bin/env python3
"""Split GT2.OVL (6 concatenated gzip members) into gt2_01..06.exe"""
import pathlib, zlib
SRC = pathlib.Path("/tmp/opencode/GT2.OVL")
OUT = pathlib.Path("/tmp/opencode/ovl_split")
MEMBERS = [
    (0x30, 144709, 316920),
    (0x23578, 44333, 248004),
    (0x2e2a8, 53389, 275780),
    (0x3b338, 5195, 11500),
    (0x3c784, 38461, 273012),
    (0x45dc4, 3602, 8416),
]

def main():
    raw = SRC.read_bytes()
    OUT.mkdir(parents=True, exist_ok=True)
    for i, (off, comp, decomp) in enumerate(MEMBERS, 1):
        data = raw[off:off+comp]
        dec = zlib.decompressobj(31)
        out = dec.decompress(data) + dec.flush()
        assert len(out) == decomp, f"member {i} {len(out)} != {decomp}"
        (OUT / f"gt2_0{i}.exe").write_bytes(out)
        (OUT / f"gt2_0{i}.exe.gz").write_bytes(data)
        print(f"gt2_0{i}.exe {len(out)} decompressed from {comp} at {off:#x}")

if __name__ == "__main__":
    main()
