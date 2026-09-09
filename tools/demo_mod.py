#!/usr/bin/env python3
"""First end-to-end GT2 mods (Phase C pipeline demo, staged/VOL-blob only).

Applies two mods and verifies them back out of the repacked blob:
1. TXD string swap: "Wrong Way" -> "FAST WAY!" (same length, span-safe)
   in /.text/data-race.txd (file 14).
2. TIM recolor: R/B channel swap of arc_topmenu TIM0 pixels (file 36),
   with per-member re-gzip at 2048-aligned slots (see gt2_gunzip_join).

Usage:
  python3 tools/demo_mod.py --iso /tmp/opencode/gt2.iso --out /tmp/vol_mod.bin
  # -> verifies: new string at file+11, swapped pixel from blob re-extract

Proven 2026-09-08: TXD bytes land at file+11; TIM pixel #25271
0x0400 -> 0x0001 after VOL->gzip->TIM->pixel round trip; blob parses
(tree walk + table monotonic via vol_pack --verify). In-game injection
(same-or-smaller sector overwrite) is future work, not attempted here.
"""
import argparse
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vol_dump import Vol, resolve_iso  # noqa: E402
from vol_pack import pack  # noqa: E402
from tim_dump import gunzip_join, tim_chain  # noqa: E402

TXD_IDX = 14
TXD_OLD = b"Wrong Way"
TXD_NEW = b"FAST WAY!"
TOPMENU_IDX = 36
TIM0_W, TIM0_H, TIM0_PX = 512, 120, 20


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=None)
    ap.add_argument("--raw-bin", default=None)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    iso = resolve_iso(args.iso or args.raw_bin)

    vol = Vol(iso)
    try:
        dc, _ = vol.header()
        tbl = vol.table(dc)

        # --- mod 1: TXD swap (same size) ---
        txd = bytearray(vol.read_file(tbl, TXD_IDX))
        at = bytes(txd).find(TXD_OLD)
        assert at == 11, f"TXD anchor moved: {at}"
        assert len(TXD_NEW) == len(TXD_OLD)
        txd[at:at + len(TXD_NEW)] = TXD_NEW

        # --- mod 2: TIM0 R/B swap + per-member re-gzip ---
        gz = vol.read_file(tbl, TOPMENU_IDX)
        raw = gunzip_join(gz)
        chain = tim_chain(raw)
        assert len(chain) == 12, f"chain {len(chain)}"
        off, flag, clut, (ox, oy, w, h, px) = chain[0]
        assert (flag & 7) == 2 and not clut and (w, h) == (TIM0_W, TIM0_H)
        px = bytearray(px)
        for i in range(0, len(px), 2):
            v = px[i] | (px[i + 1] << 8)
            r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
            nv = b | (g << 5) | (r << 10) | (v & 0x8000)
            px[i], px[i + 1] = nv & 0xFF, nv >> 8
        # re-slice: TIM0 header (20B) + new pixels, members re-gzipped
        slices, pos = [], 0
        for j, (o, f2, c2, (x2, y2, w2, h2, p2)) in enumerate(chain):
            ln = 20 + len(p2) if not c2 else None
            assert ln is not None  # arc_topmenu has no CLUTs
            sl = raw[pos:pos + ln]
            if j == 0:
                sl = raw[pos:pos + 20] + bytes(px)
            slices.append(sl)
            pos += ln
        assert pos == len(raw), "chain covers file"
        members = []
        for s in slices:
            co = zlib.compressobj(9, zlib.DEFLATED, 31)
            members.append(co.compress(s) + co.flush())
        newgz = bytearray()
        for m in members:
            newgz += m
            newgz += b"\0" * ((-len(newgz)) % 2048)

        blob, new_tbl = pack(vol, tbl, dc,
                             {TXD_IDX: bytes(txd), TOPMENU_IDX: bytes(newgz)})
        with open(args.out, "wb") as f:
            f.write(blob)

        # --- verify back out of the blob ---
        assert blob[tbl[TXD_IDX] + 11:tbl[TXD_IDX] + 20] == TXD_NEW
        s, e = new_tbl[TOPMENU_IDX], new_tbl[TOPMENU_IDX + 1]
        raw2 = gunzip_join(blob[s:e])
        assert len(raw2) == len(raw)
        # R/B swap check on the first r!=b pixel of the ORIGINAL chain
        # (`raw` still holds pre-mod bytes; TIM0 starts at file offset 0).
        orig = raw[TIM0_PX:TIM0_PX + TIM0_W * TIM0_H * 2]
        new = raw2[TIM0_PX:TIM0_PX + TIM0_W * TIM0_H * 2]
        oi = next(i // 2 for i in range(0, len(orig), 2)
                  if (orig[i] | (orig[i + 1] << 8)) & 31 !=
                  ((orig[i] | (orig[i + 1] << 8)) >> 10) & 31)
        ov = orig[2 * oi] | (orig[2 * oi + 1] << 8)
        nv = new[2 * oi] | (new[2 * oi + 1] << 8)
        assert (nv & 31, (nv >> 10) & 31) == ((ov >> 10) & 31, ov & 31)
        print(f"wrote {args.out} ({len(blob)} bytes)")
        print("verify ok: TXD string + R/B-swapped pixel in blob")
    finally:
        vol.close()


if __name__ == "__main__":
    main()
