#!/usr/bin/env python3
"""Repack GT2.VOL with replaced files (independent implementation of the
gt2_vol_pack C API in decomp/src/vol/vol.c — same layout rules, verified
by hash equality on identical inputs; see decomp/tests/test_vol_pack.c).

Layout rules (US 1.2 sim, see decomp/docs/gtfs_notes.md):
- tbl[] holds plain byte offsets, essentially unaligned: files pack
  tightly, no padding. The array spans [0x10, 0xB508), so its last 32
  entries live at [0xB488, 0xB508) — no separate copy exists.
- Files 0 ([0x2FC,0xBB80)) and 1 ([0xBB80,0x66B22)) carry the tables and
  are never replaced (slot tree + flat dir live inside them and name
  FILES, not offsets, so they survive resizes untouched; the embedded
  tbl bytes are rewritten as part of the array stamp).
- Final marker tbl[data_count] is 0 (degenerate last file); preserved.

Usage:
  python3 tools/vol_pack.py --iso /tmp/opencode/gt2.iso --out /tmp/vol_new.bin
  python3 tools/vol_pack.py --iso IMG --replace arc_topmenu=hd/bg.gz
      --replace-index 11559=extra/blob.bin --out /tmp/vol_new.bin --report
  python3 tools/vol_pack.py --iso IMG --out /tmp/vol_new.bin --verify
      # zero-rep round trip must be byte-identical to the source extent
"""
import argparse
import hashlib
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vol_dump import Vol  # noqa: E402  (reuse raw/cooked reader + layout)


def pack(vol, tbl, dc, reps):
    """reps: {index: bytes}. Returns (blob, new_tbl)."""
    for idx in reps:
        if idx < 2 or idx > dc - 2:
            raise ValueError(f"index {idx} not replaceable (need 2..{dc - 2})")
    new_tbl = list(tbl)
    cursor = tbl[2]
    chunks = []   # (offset_in_blob, bytes)
    for i in range(2, dc - 1):
        new_tbl[i] = cursor
        data = reps.get(i)
        if data is None:
            start, end = tbl[i], tbl[i + 1]
            if end < start:
                raise ValueError(f"corrupt source range idx {i}")
            if end > start:
                chunks.append((cursor, vol._pread(start, end - start)))
            cursor += end - start
        else:
            if data:
                chunks.append((cursor, bytes(data)))
            cursor += len(data)
    new_tbl[dc - 1] = cursor
    new_tbl[dc] = tbl[dc]   # degenerate end marker, preserved
    # Prefix [0, tbl[2]) verbatim (header + tables + files 0/1).
    blob = bytearray(vol._pread(0, tbl[2]))
    # NOTE: the tbl array spans [0x10, 0x10+4*(dc+1)) = [0x10, 0xB508), so
    # the last 32 entries at [0xB488, 0xB508) are part of the array itself
    # (not a separate copy): the stamp below covers them. Assert the model
    # against the source first (fail closed on unknown layouts).
    if len(tbl) >= 32:
        srctail = vol._pread(0xB488, 128)
        if srctail != struct.pack("<32I", *tbl[dc + 1 - 32:]):
            raise ValueError("table-end mismatch: unknown layout")
    blob[0x10:0x10 + 4 * (dc + 1)] = struct.pack(f"<{dc + 1}I", *new_tbl)
    for off, data in chunks:
        blob[off:off + len(data)] = data
    assert len(blob) == cursor, (len(blob), cursor)
    return bytes(blob), new_tbl


def verify(blob, tbl, dc, new_tbl, reps):
    assert blob[:4] == b"GTFS", "magic"
    got = struct.unpack(f"<{dc + 1}I", blob[0x10:0x10 + 4 * (dc + 1)])
    assert list(got) == new_tbl, "table stamped (incl. last-32 at B488)"
    for idx, data in reps.items():
        s, e = new_tbl[idx], new_tbl[idx + 1]
        assert blob[s:e] == data, f"file {idx} bytes"
    # monotonic except the degenerate final marker
    assert all(b >= a for a, b in zip(new_tbl, new_tbl[1:-1])), "monotonic"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=None)
    ap.add_argument("--raw-bin", default=None)
    ap.add_argument("--replace", action="append", default=[],
                    help="NAME=FILE (flat VOL name)")
    ap.add_argument("--replace-index", action="append", default=[],
                    help="N=FILE (data-file index)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--verify", action="store_true",
                    help="re-parse the output blob and check it")
    args = ap.parse_args()

    src = args.iso or args.raw_bin
    if not src:
        ap.error("need --iso or --raw-bin")
    vol = Vol(src)
    try:
        dc, ec = vol.header()
        tbl = vol.table(dc)
        names = {n: i for n, i, _ in vol.names()}

        reps = {}
        for spec in args.replace:
            name, path = spec.split("=", 1)
            if name not in names:
                ap.error(f"unknown flat name: {name}")
            with open(path, "rb") as f:
                reps[names[name]] = f.read()
        for spec in args.replace_index:
            idx, path = spec.split("=", 1)
            with open(path, "rb") as f:
                reps[int(idx)] = f.read()

        blob, new_tbl = pack(vol, tbl, dc, reps)
        with open(args.out, "wb") as f:
            f.write(blob)

        if args.report or reps:
            for idx in sorted(reps):
                print(f"idx {idx}: {tbl[idx + 1] - tbl[idx]} -> "
                      f"{len(reps[idx])} bytes "
                      f"(off {new_tbl[idx]:08x})")
            print(f"blob {len(blob)} bytes "
                  f"(source max end {max(tbl):08x})")
        if args.verify:
            verify(blob, tbl, dc, new_tbl, reps)
            print("verify ok")
        print(f"sha256: {hashlib.sha256(blob).hexdigest()}")
    finally:
        vol.close()


if __name__ == "__main__":
    main()
