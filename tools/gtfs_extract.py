#!/usr/bin/env python3
"""GTFS extractor - lists and extracts VOL files via idx -> tbl mapping"""
import struct, pathlib, os, argparse
ISO = pathlib.Path("/tmp/opencode/gt2.iso")
BASE = 473*2048

def load_tbl():
    with open(ISO,'rb') as f:
        f.seek(BASE+0x10)
        raw=f.read(0x4000)
        # interpret as array of u32 offsets (start positions)
        # count guess: until entry > VOL size or not monotonic
        tbl=[]
        for i in range(0, 2048):
            v=struct.unpack_from('<I', raw, i*4)[0]
            tbl.append(v)
            # break if past VOL size (488M) and not plausible
            if v>500_000_000 and i>100:
                break
        # Trim to monotonic prefix
        # Find where monotonic breaks: tbl should be increasing
        # Keep until first decrease
        mono=[]
        for v in tbl:
            if not mono or v>mono[-1]:
                mono.append(v)
            else:
                break
        return mono

def load_dir():
    entries=[]
    with open(ISO,'rb') as f:
        f.seek(BASE+0xBE7C)
        # dir goes until offset reaches next tbl start (0x66B22) or null entry
        data=f.read(0x60000)  # read chunk covering dir
        off=0
        while off+11 < len(data):
            zero=struct.unpack_from('<I', data, off)[0]
            if zero!=0 and zero!=0xFFFFFFFF:
                # maybe misaligned due to variable padding, try resync by searching for 00 00 00 00 pattern
                # but assume fixed 32 stride if off%32==0
                pass
            h=struct.unpack_from('<I', data, off+4)[0]
            idx=struct.unpack_from('<H', data, off+8)[0]
            # byte at 10 is pad, filename starts at 11
            # Find null-terminated filename from 11
            fname_end=data.find(b'\x00', off+11)
            if fname_end==-1:
                break
            fname=data[off+11:fname_end].decode(errors='ignore')
            if not fname:
                break
            # Heuristic: entry size is aligned to 4 or 32? Try to compute next entry start as (fname_end+4)&~3 or next 32 boundary
            # For GT2, entries appear 32-byte fixed regardless of name length (observed). Use 32 stride for simplicity.
            entries.append((idx, h, fname, off))
            off+=32
            # Stop if next bytes are all zeros for 32 bytes
            if off+32 < len(data) and data[off:off+32]==b'\x00'*32:
                break
            # Also stop if off exceeds known dir size (0x66B22-0xBE7C ~ 371k) but our loop 0x60000 covers
            if off>0x60000:
                break
    return entries

def extract(idx, out_path, tbl):
    with open(ISO,'rb') as f:
        # start = tbl[idx], end = tbl[idx+1] if idx+1 < len(tbl) else VOL end
        if idx>=len(tbl):
            print(f"idx {idx} out of range {len(tbl)}")
            return False
        start=tbl[idx]
        if idx+1 < len(tbl):
            end=tbl[idx+1]
            size=end-start
        else:
            # last file to end of VOL
            f.seek(0,2)
            vol_end=os.path.getsize(ISO)
            size=vol_end - (BASE+start)
        f.seek(BASE+start)
        data=f.read(size)
        pathlib.Path(out_path).parent.mkdir(parents=True, exist_ok=True)
        pathlib.Path(out_path).write_bytes(data)
        print(f"idx {idx}: {start:08x}->{end:08x} size {size} -> {out_path} head {data[:16].hex()}")
        return True

if __name__=="__main__":
    tbl=load_tbl()
    print(f"tbl entries {len(tbl)} first 40:")
    for i in range(min(40,len(tbl))):
        nxt=tbl[i+1] if i+1<len(tbl) else 0
        print(f" {i:3}: {tbl[i]:08x} size {nxt-tbl[i] if nxt else 0:08x} ({nxt-tbl[i] if nxt else 0})")
    entries=load_dir()
    print(f"\ndir entries {len(entries)}:")
    for idx,h,fname,off in entries[:20]:
        print(f" idx {idx:4} h {h:08x} {fname:24} @dir {off:04x} -> tbl off {tbl[idx]:08x}" if idx < len(tbl) else f" idx {idx} out of tbl range")
    # CLI
    import sys
    if len(sys.argv)>1 and sys.argv[1]=="extract":
        outdir=pathlib.Path(sys.argv[2]) if len(sys.argv)>2 else pathlib.Path("/tmp/opencode/gt2_vol_extract")
        outdir.mkdir(parents=True, exist_ok=True)
        for idx,h,fname,_ in entries[:10]:
            extract(idx, outdir/fname, tbl)
        print(f"extracted 10 files to {outdir}")
    # Quick sanity: try to list all and find TIM magic
    # for idx,h,fname,_ in entries:
    #     if "tim" in fname:
    #         extract(idx, f"/tmp/opencode/tim_{fname}", tbl); break
