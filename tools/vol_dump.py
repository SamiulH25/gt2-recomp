#!/usr/bin/env python3
"""GT2.VOL inspector for decomp work.

Works on a cooked 2048-B/sector ISO *or* a raw 2352-B/sector .bin dump
(auto-detected by size). No hardcoded paths; GT2_ISO env is the default.

  list [--prefix P] [--limit N]      list named files
  find NAME                           print idx / offset / size
  extract NAME OUT|--index I OUT      extract one file (by name or tbl idx)
  tbl [--limit N]                     dump the offset table head
  cook RAW.BIN COOKED.ISO             2352 -> 2048 conversion

VOL base = LBA 473. See decomp/docs/gtfs_notes.md for the format.
"""
import argparse
import os
import struct
import sys

SECTOR = 2048
RAW_SECTOR = 2352
VOL_LBA = 473
VOL_DIR_OFF = 0xBE7C
DIR_REC = 32


def cook(raw_path, out_path):
    with open(raw_path, "rb") as fin, open(out_path, "wb") as fout:
        size = os.path.getsize(raw_path)
        sectors = size // RAW_SECTOR
        for _ in range(sectors):
            s = fin.read(RAW_SECTOR)
            if len(s) < RAW_SECTOR:
                break
            fout.write(s[24:24 + SECTOR] if s[15] == 2 else s[16:16 + SECTOR])
    print(f"cooked {sectors} sectors -> {out_path}")


class Vol:
    def __init__(self, path):
        self.path = path
        # Raw 2352 dumps of this disc are 691850208 bytes; anything with a
        # 2352-multiple size that is NOT a 2048 multiple is treated as raw.
        size = os.path.getsize(path)
        self.raw = size % RAW_SECTOR == 0 and size % SECTOR != 0
        self.f = open(path, "rb")

    def close(self):
        self.f.close()

    def _pread(self, off, n):
        if not self.raw:
            self.f.seek(VOL_LBA * SECTOR + off)
            return self.f.read(n)
        # Raw: VOL byte X -> sector 473+X//2048, payload at +24.
        out = bytearray()
        while n > 0:
            sec = VOL_LBA + off // SECTOR
            ino = off % SECTOR
            take = min(n, SECTOR - ino)
            self.f.seek(sec * RAW_SECTOR + 24 + ino)
            chunk = self.f.read(take)
            if not chunk:
                break
            out += chunk
            off += len(chunk)
            n -= len(chunk)
        return bytes(out)

    def header(self):
        hdr = self._pread(0, 16)
        magic, _, dc, ec, _ = struct.unpack("<IIHHI", hdr)
        assert magic == 0x53465447, f"bad GTFS magic {magic:08x}"
        return dc, ec

    def table(self, dc):
        raw = self._pread(0x10, (dc + 1) * 4)
        return list(struct.unpack(f"<{dc + 1}I", raw))

    def names(self):
        recs = []
        i = 0
        while True:
            rec = self._pread(VOL_DIR_OFF + i * DIR_REC, DIR_REC)
            if len(rec) < DIR_REC or rec == b"\x00" * DIR_REC:
                return recs
            zero, h, idx = struct.unpack_from("<IIH", rec, 0)
            name = rec[11:32].split(b"\x00")[0].decode()
            # NOTE: 34 records have a nonzero first word ('z'/'gz' overflow
            # from >20-char names truncated into the 21-byte field) but a
            # valid idx. Keep them; see decomp/docs/gtfs_notes.md Q9.
            if name:
                recs.append((name, idx, h))
            i += 1

    def read_file(self, tbl, idx):
        if idx >= len(tbl) - 1:
            raise IndexError(f"tbl idx {idx} out of range {len(tbl)}")
        start, end = tbl[idx], tbl[idx + 1]
        if end < start:
            raise ValueError(f"corrupt range idx {idx}")
        return self._pread(start, end - start)

    def slots(self, count):
        """Unified entry table: 32B slots from VOL+0xB800."""
        out = []
        for i in range(count):
            rec = self._pread(0xB800 + i * 32, 32)
            date, nxt = struct.unpack_from("<IH", rec, 0)
            out.append((date, nxt, rec[6], rec[7:32].split(b"\x00")[0]))
        return out

    def walk(self, slots, path):
        """Hierarchical resolve -> (slot, date, next, flags, name).
        Mirrors SCUS search_vol_dir; raises KeyError/IndexError."""
        comps = [c for c in path.split("/") if c]
        if not comps:
            raise KeyError("empty path")
        cur = 0
        for ci, comp in enumerate(comps):
            last = ci == len(comps) - 1
            comp = comp.encode()
            i = cur
            while True:
                date, nxt, flags, name = slots[i]
                if name == comp:
                    break
                if flags & 0x80:
                    raise KeyError(f"not found: {comp!r}")
                i += 1
            if last:
                return i, date, nxt, flags, name
            if not flags & 0x01:
                raise KeyError(f"not a dir: {comp!r}")
            cur = nxt
        raise AssertionError("unreachable")


def resolve_iso(arg):
    if arg:
        return arg
    env = os.environ.get("GT2_ISO")
    if env:
        return env
    default = "/tmp/opencode/gt2.iso"
    if os.path.exists(default):
        return default
    sys.exit("no image: pass --iso or set GT2_ISO")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=None)
    ap.add_argument("--raw-bin", default=None,
                    help="shorthand for --iso <raw 2352 dump>")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("list")
    p.add_argument("--prefix", default="")
    p.add_argument("--limit", type=int, default=30)

    p = sub.add_parser("find")
    p.add_argument("name")

    p = sub.add_parser("extract")
    p.add_argument("name", nargs="?")
    p.add_argument("--index", type=int, default=None)
    p.add_argument("out")

    p = sub.add_parser("tbl")
    p.add_argument("--limit", type=int, default=12)

    p = sub.add_parser("walk")
    p.add_argument("path", help="hierarchical path, e.g. /arcade/arc_carlogo")

    p = sub.add_parser("cook")
    p.add_argument("raw")
    p.add_argument("out")

    a = ap.parse_args()
    if a.cmd == "cook":
        cook(a.raw, a.out)
        return

    iso = a.raw_bin or resolve_iso(a.iso)
    v = Vol(iso)
    dc, ec = v.header()
    print(f"{iso}: raw2352={v.raw} data_count={dc} entry_count={ec}")
    tbl = v.table(dc)

    if a.cmd == "tbl":
        for i in range(min(a.limit, dc + 1)):
            print(f"tbl[{i:5}]: {tbl[i]:08x} size={tbl[i+1]-tbl[i] if i < dc else 0}")
    elif a.cmd == "list":
        recs = v.names()
        shown = 0
        for name, idx, h in recs:
            if name.startswith(a.prefix):
                print(f"idx {idx:5} h {h:08x} {name} "
                      f"off {tbl[idx]:08x} size {tbl[idx+1]-tbl[idx]}")
                shown += 1
                if shown >= a.limit:
                    break
        print(f"({len(recs)} named records)")
    elif a.cmd == "find":
        for name, idx, h in v.names():
            if name == a.name:
                print(f"idx={idx} h={h:08x} off={tbl[idx]:08x} "
                      f"size={tbl[idx+1]-tbl[idx]}")
                return
        sys.exit(f"not found: {a.name}")
    elif a.cmd == "extract":
        idx = a.index
        if idx is None:
            for name, i, h in v.names():
                if name == a.name:
                    idx = i
                    break
            if idx is None:
                sys.exit(f"not found: {a.name}")
        data = v.read_file(tbl, idx)
        with open(a.out, "wb") as f:
            f.write(data)
        print(f"idx {idx}: {len(data)} bytes -> {a.out} head {data[:8].hex()}")
    elif a.cmd == "walk":
        slots = v.slots(ec)
        try:
            i, date, nxt, flags, name = v.walk(slots, a.path)
        except (KeyError, IndexError) as ex:
            sys.exit(f"walk {a.path!r}: {ex}")
        print(f"slot={i} date={date:08x} tbl={nxt} flags={flags:#04x} "
              f"name={name.decode()}")
        if not flags & 0x01 and nxt < len(tbl) - 1:
            print(f"data: off={tbl[nxt]:08x} size={tbl[nxt+1]-tbl[nxt]}")
    v.close()


if __name__ == "__main__":
    main()
