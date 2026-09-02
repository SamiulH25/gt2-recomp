#!/usr/bin/env python3
"""
GT2 Hybrid Recomp - Extraction Pipeline
Handles: 2352->2048 conversion, ISO extract, OVL decompress, GTFS parse
"""
import os, struct, subprocess, pathlib, gzip, re

ROOT = pathlib.Path(__file__).parent.parent
BIN = ROOT / "Gran Turismo 2 (USA) (Simulation Mode) (v1.2)" / "Gran Turismo 2 (USA) (Simulation Mode) (v1.2).bin"
ISO = pathlib.Path("/tmp/opencode/gt2.iso")
OUT = pathlib.Path("/tmp/opencode/gt2_extract")
SCUS_OUT = pathlib.Path("/tmp/opencode/SCUS_944.88")
OVL_OUT = pathlib.Path("/tmp/opencode/GT2.OVL")
OVL_DEC = pathlib.Path("/tmp/opencode/GT2.OVL.decompressed")

def bin_to_iso():
    print(f"[1/4] Converting 2352 -> 2048: {BIN} -> {ISO}")
    ISO.parent.mkdir(parents=True, exist_ok=True)
    with open(BIN,'rb') as fin, open(ISO,'wb') as fout:
        sectors = os.path.getsize(BIN)//2352
        for n in range(sectors):
            s = fin.read(2352)
            mode = s[15]
            if mode == 2:
                fout.write(s[24:24+2048])
            else:
                fout.write(s[16:16+2048])
    print(f"  -> {ISO} {ISO.stat().st_size} bytes ({ISO.stat().st_size//2048} sectors)")
    r = subprocess.run(["isoinfo","-d","-i",str(ISO)], capture_output=True, text=True)
    print(r.stdout)

def extract_system_files():
    print(f"[2/4] Extracting SYSTEM.CNF / SCUS_944.88 / GT2.OVL / GT2.VOL header")
    OUT.mkdir(parents=True, exist_ok=True)
    for name in ["SCUS_944.88;1", "SYSTEM.CNF;1", "GT2.OVL;1"]:
        r = subprocess.run(["isoinfo","-R","-i",str(ISO),"-x",f"/{name}"], capture_output=True)
        out = OUT / name.replace(";1","")
        out.write_bytes(r.stdout)
        print(f"  {name} -> {out} ({len(r.stdout)} bytes)")
        if "SCUS" in name:
            SCUS_OUT.write_bytes(r.stdout)
        if "OVL" in name:
            OVL_OUT.write_bytes(r.stdout)
    # also dump VOL header via raw read (too big for isoinfo pipe efficiently, do sector copy)
    # Use isoinfo to get LBA: we know 473
    with open(ISO,'rb') as f:
        f.seek(473*2048)
        hdr = f.read(8192)
        (OUT / "GT2.VOL.header.bin").write_bytes(hdr)
        print(f"  GT2.VOL header (8192 bytes): {hdr[:8]!r} magic={hdr[:4]}")
    r = subprocess.run(["isoinfo","-l","-R","-i",str(ISO)], capture_output=True, text=True)
    print(r.stdout[:3000])

def decompress_ovl():
    print(f"[3/4] Decompressing GT2.OVL (gzip)")
    data = OVL_OUT.read_bytes()
    print(f"  raw {len(data)} bytes, head {data[:16].hex()}")
    # Find gzip streams - OVL has 1f 8b at various offsets?
    off = data.find(b'\x1f\x8b\x08')
    print(f"  first gzip at offset {off}")
    if off >= 0:
        # may be multiple concatenated gzip? Try decompress from off
        import io
        try:
            dec = gzip.decompress(data[off:])
            OVL_DEC.write_bytes(dec)
            print(f"  decompressed {len(dec)} bytes -> {OVL_DEC}")
            print(f"  dec head {dec[:64].hex()}")
            # check if PS-X EXE inside?
            if dec[:8]==b'PS-X EXE':
                print("  -> decompressed is PS-X EXE")
            else:
                # scan for code patterns
                strs = re.findall(b"[ -~]{4,}", dec[:1024])
                print(f"  strs preview: {strs[:10]}")
        except Exception as e:
            print(f"  gzip fail: {e}")
            # try incremental
            with open(OVL_OUT,'rb') as f:
                raw = f.read()
            # OVL format: first 0x30 header then gzip payload?
            hdr = raw[:0x30]
            print(f"  OVL hdr 0x30: {hdr.hex()}")
            # struct: 0x00 unknown, 0x04 maybe decompressed size?
            vals = struct.unpack('<8I', hdr[:32])
            print(f"  hdr vals: {[hex(v) for v in vals]}")
            # try decompress after hdr
            for try_off in [0x30, off]:
                try:
                    dec = gzip.decompress(raw[try_off:])
                    print(f"  decompress at {try_off:x} success {len(dec)}")
                    OVL_DEC.write_bytes(dec)
                    break
                except Exception as e2:
                    print(f"  fail at {try_off:x}: {e2}")
    else:
        print("  no gzip found, treating as raw")

def parse_gtfs():
    print(f"[4/4] Parsing GT2.VOL GTFS")
    with open(ISO,'rb') as f:
        f.seek(473*2048)
        hdr = f.read(0x1000)
    magic = hdr[0:4]
    print(f"  magic: {magic} (expect GTFS)")
    # layout guess from earlier dump: GTFS + counts + index table
    # dump first 128 entries as u32
    vals = struct.unpack_from('<32I', hdr, 0)
    print(f"  hdr u32[0:16]: {[hex(v) for v in vals[:16]]}")
    # Look for table at 0x10? Earlier dump showed entries like fc 02 00 00 etc
    # Parse as file table: offset/size pairs?
    # We'll just dump structured
    off = 0x10
    for i in range(8):
        entry = struct.unpack_from('<II', hdr, off+i*8)
        print(f"  entry {i} @ {off+i*8:x}: off={entry[0]:x} size={entry[1]:x} ({entry[1]})")
    # Need longer view - dump to file for Ghidra/reimpl
    pathlib.Path("/tmp/opencode/gtfs_dump.txt").write_text(
        "\n".join(f"{i*4:04x}: {v:08x} {v}" for i,v in enumerate(vals))
    )

if __name__ == "__main__":
    bin_to_iso()
    extract_system_files()
    decompress_ovl()
    parse_gtfs()
    print("\nDone. Outputs in /tmp/opencode/")
