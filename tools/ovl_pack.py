#!/usr/bin/env python3
"""Repack GT2.OVL (6 concatenated gzip members + 12-word header).

Container (see decomp/src/ovl/ovl.c):
  u32 hdr_size (=0x30); then per member i in 0..5: comp_size[i] at
  words[1+2i]; member offsets off[i] for i in 1..5 at words[2i]
  (member 0 starts at hdr_size, member 5 runs to EOF).

Usage:
  ovl_pack.py --iso IMG --out OVLNEW                       # byte-identical rebuild
  ovl_pack.py --iso IMG --replace-member 2=gt2_02.exe --out OVLNEW
      # raw (inflated) bytes are re-gzipped (level 9 default, --level N)
  ovl_pack.py --iso IMG --out OVLNEW --verify              # reparse + inflate-compare

Members inflate to overlays/gt2_0{1..6}.exe (see tools/split_ovl.py).
"""
import argparse
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vol_dump import Vol, resolve_iso  # noqa: E402

MEMBERS = 6
HDR_WORDS = 12


def read_ovl(iso):
    v = Vol(iso)
    try:
        from vol_dump import VOL_LBA, SECTOR
        f = v.f
        # Locate GT2.OVL;1 via the ISO9660 root dir (record layout:
        # len@0, extentLE@2, sizeLE@10, namelen@32, name@33).
        f.seek(16 * SECTOR)
        pvd = f.read(2048)
        assert pvd[1:6] == b"CD001", "no PVD"
        root = pvd[156:156 + 34]
        assert root[0] == 34, "bad root record"
        extent = struct.unpack_from("<I", root, 2)[0]
        size = struct.unpack_from("<I", root, 10)[0]
        # walk root dir for GT2.OVL;1
        f.seek(extent * SECTOR)
        data = f.read(size)
        p = 0
        ovl_ext = ovl_size = None
        while p < len(data):
            rl = data[p]
            if rl == 0:
                break
            ex = struct.unpack_from("<I", data, p + 2)[0]
            sz = struct.unpack_from("<I", data, p + 10)[0]
            nl = data[p + 32]
            nm = data[p + 33:p + 33 + nl].decode()
            if nm.startswith("GT2.OVL"):
                ovl_ext, ovl_size = ex, sz
                break
            p += rl
        assert ovl_ext is not None, "GT2.OVL;1 not in root dir"
        f.seek(ovl_ext * SECTOR)
        return f.read(ovl_size)
    finally:
        v.close()


def parse(blob):
    words = list(struct.unpack("<12I", blob[:48]))
    assert words[0] == 0x30, f"hdr_size {words[0]:08x}"
    comp = [words[1 + 2 * i] for i in range(6)]
    off = [words[0]] + [words[2 * i] for i in range(1, 6)]
    for i in range(6):
        end = off[i + 1] if i + 1 < 6 else len(blob)
        assert blob[off[i]:off[i] + 2] == b"\x1f\x8b", f"member {i} magic"
        assert off[i] + comp[i] <= end, f"member {i} range"
    return comp, off


def build(members_comp):
    """members_comp: 6 gzip blobs -> full OVL file. Members are followed
    by zero padding to 4-byte alignment (1-3 bytes on this image)."""
    assert len(members_comp) == 6
    # words layout: [hdr, c0, o1, c1, o2, c2, o3, c3, o4, c4, o5, c5]
    hdr = [0x30] + [0] * 11
    parts = []
    cur = 0x30
    for i, m in enumerate(members_comp):
        hdr[1 + 2 * i] = len(m)   # comp_size[i]
        if i > 0:
            hdr[2 * i] = cur       # off[i]
        parts.append(m)
        cur += len(m)
        pad = (-cur) % 4
        if pad:
            parts.append(b"\0" * pad)
            cur += pad
    return struct.pack("<12I", *hdr) + b"".join(parts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=None)
    ap.add_argument("--raw-bin", default=None)
    ap.add_argument("--replace-member", action="append", default=[],
                    help="I=FILE (raw inflated bytes, re-gzipped)")
    ap.add_argument("--level", type=int, default=9)
    ap.add_argument("--out", required=True)
    ap.add_argument("--verify", action="store_true")
    args = ap.parse_args()
    iso = resolve_iso(args.iso or args.raw_bin)

    blob = read_ovl(iso)
    comp, off = parse(blob)
    members = [blob[off[i]:off[i] + comp[i]] for i in range(6)]
    inflated = [zlib.decompress(m, 31) for m in members]
    print("members:", " ".join(
        f"{i}:{len(members[i])}->{len(inflated[i])}" for i in range(6)))

    for spec in args.replace_member:
        idx, path = spec.split("=", 1)
        with open(path, "rb") as f:
            raw = f.read()
        co = zlib.compressobj(args.level, zlib.DEFLATED, 31)
        members[int(idx)] = co.compress(raw) + co.flush()
        print(f"member {idx}: replaced with {len(raw)} raw bytes")

    out = build(members)
    with open(args.out, "wb") as f:
        f.write(out)
    print(f"wrote {args.out} ({len(out)} bytes, source {len(blob)})")

    if args.verify or not args.replace_member:
        comp2, off2 = parse(out)
        for i in range(6):
            got = zlib.decompress(out[off2[i]:off2[i] + comp2[i]], 31)
            want = inflated[i] if not any(
                s.startswith(f"{i}=") for s in args.replace_member) else None
            if want is not None:
                assert got == want, f"member {i} mismatch"
        if not args.replace_member:
            assert out == blob, "round trip differs"
            print("verify ok: byte-identical round trip")
        else:
            print("verify ok: members inflate, replaced differ as expected")


if __name__ == "__main__":
    main()
