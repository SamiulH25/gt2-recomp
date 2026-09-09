#!/usr/bin/env python3
"""Inspect GT2 texture/string assets (companion to decomp gt2/tim.h).

  tim_dump.py info arc_topmenu [--iso IMG]      # chained TIM table
  tim_dump.py ppm arc_topmenu 0 out.ppm         # decode TIM idx -> PPM
  tim_dump.py logo /carlogo/a-a7rl--.tim        # container split report
  tim_dump.py txd /.text/data-race.txd [N]      # cat strings (N max)
  tim_dump.py raw champtim.tim                  # opaque-container report

The C port (decomp/src/tim/tim.c + tests/test_tim.c) is authoritative;
this tool mirrors its parsing for quick inspection and PPM export.
"""
import argparse
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vol_dump import Vol, resolve_iso  # noqa: E402


def tim_chain(d):
    """Yield (off, flag, clut_or_None, img_dict) with strict validation."""
    off, out = 0, []
    while off + 8 <= len(d):
        magic, flag = struct.unpack_from("<II", d, off)
        if magic != 0x10 or flag > 0x0F:
            break
        p = off + 8
        clut = None
        if flag & 8:
            ln, ox, oy, w, h = struct.unpack_from("<IHHHH", d, p)
            clut = (ox, oy, w, h, d[p + 12:p + ln])
            p += ln
        ln, ox, oy, w, h = struct.unpack_from("<IHHHH", d, p)
        out.append((off, flag, clut, (ox, oy, w, h, d[p + 12:p + ln])))
        p += ln
        off = p
    return out


def gunzip_join(blob):
    """Concatenated gzip members at 2048-aligned slots (see gt2_gunzip_join)."""
    out, off = bytearray(), 0
    while off + 2 <= len(blob):
        if blob[off:off + 2] != b"\x1f\x8b":
            break
        de = zlib.decompressobj(31)
        out += de.decompress(blob[off:])
        out += de.flush()
        off += len(blob[off:]) - len(de.unused_data)
        off = (off + 2047) & ~2047
    return bytes(out)


def read_vol(iso, spec):
    """spec: flat name or /hier/path (auto-detect). Returns bytes."""
    v = Vol(iso)
    try:
        dc, ec = v.header()
        tbl = v.table(dc)
        if spec.startswith("/"):
            slots = v.slots(ec)
            _, _, nxt, flags, _ = v.walk(slots, spec)
            if flags & 0x01:
                raise KeyError(f"is a directory: {spec}")
            return v.read_file(tbl, nxt)
        for name, idx, _ in v.names():
            if name == spec:
                return v.read_file(tbl, idx)
        raise KeyError(f"unknown flat name: {spec}")
    finally:
        v.close()


def decode16(px, w, h):
    rgb = bytearray(w * h * 3)
    for i in range(w * h):
        v = px[2 * i] | (px[2 * i + 1] << 8)
        rgb[3 * i] = (v & 31) * 255 // 31
        rgb[3 * i + 1] = ((v >> 5) & 31) * 255 // 31
        rgb[3 * i + 2] = ((v >> 10) & 31) * 255 // 31
    return rgb


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=None)
    ap.add_argument("--raw-bin", default=None)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("info")
    p.add_argument("name")

    p = sub.add_parser("ppm")
    p.add_argument("name")
    p.add_argument("index", type=int)
    p.add_argument("out")

    p = sub.add_parser("logo")
    p.add_argument("path")

    p = sub.add_parser("txd")
    p.add_argument("path")
    p.add_argument("limit", nargs="?", type=int, default=30)

    p = sub.add_parser("raw")
    p.add_argument("name")

    args = ap.parse_args()
    iso = resolve_iso(args.iso or args.raw_bin)

    if args.cmd == "info":
        d = read_vol(iso, args.name)
        raw = gunzip_join(d) if d[:2] == b"\x1f\x8b" else d
        print(f"{args.name}: {len(d)} bytes"
              f"{' (gzip members)' if raw is not d else ''} -> {len(raw)} raw")
        for i, (off, flag, clut, (ox, oy, w, h, px)) in enumerate(
                tim_chain(raw)):
            print(f"  [{i}] @{off}: mode={flag & 7} "
                  f"clut={'%dx%d' % (clut[2], clut[3]) if clut else 'no'} "
                  f"img {w}x{h} org=({ox},{oy}) {len(px)}B")
    elif args.cmd == "ppm":
        d = read_vol(iso, args.name)
        raw = gunzip_join(d) if d[:2] == b"\x1f\x8b" else d
        chain = tim_chain(raw)
        off, flag, clut, (ox, oy, w, h, px) = chain[args.index]
        assert (flag & 7) == 2 and not clut, "PPM export: 16-bit only for now"
        with open(args.out, "wb") as f:
            f.write(f"P6\n{w} {h}\n255\n".encode())
            f.write(decode16(px, w, h))
        print(f"wrote {args.out} ({w}x{h})")
    elif args.cmd == "logo":
        d = read_vol(iso, args.path)
        cands = []
        for off in range(0, len(d) - 20, 4):
            if d[off:off + 4] != b"\x10\x00\x00\x00":
                continue
            try:
                flag = struct.unpack_from("<I", d, off + 4)[0]
                if flag & ~0x0F or (flag & 7) > 3 or not flag & 8:
                    continue
                ln, _, _, w, h = struct.unpack_from("<IHHHH", d, off + 8)
                cands.append((off, flag, w, h))
            except struct.error:
                pass
        print(f"{args.path}: {len(d)} bytes, CLUT-TIM candidates: {cands}")
    elif args.cmd == "txd":
        d = read_vol(iso, args.path)
        n = 0
        for s in d.split(b"\0"):
            if not s:
                continue
            try:
                print(s.decode("ascii"))
            except UnicodeDecodeError:
                print(repr(s))
            n += 1
            if n >= args.limit:
                break
        print(f"({n} shown)")
    elif args.cmd == "raw":
        d = read_vol(iso, args.name)
        print(f"{args.name}: {len(d)} bytes head={d[:16].hex()}")
        nz = sum(1 for i in range(0, len(d), 16) if d[i:i + 16] != b"\0" * 16)
        print(f"nonzero 16B blocks: {nz}/{len(d) // 16}")


if __name__ == "__main__":
    main()
