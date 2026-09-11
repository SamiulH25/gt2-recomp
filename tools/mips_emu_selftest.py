#!/usr/bin/env python3
"""Self-test for tools/mips_emu.py: ALU, delay slots, unaligned accesses."""
import struct
import sys

sys.path.insert(0, ".")
from tools.mips_emu import Emu

RET = [0x03E00008, 0x00000000]  # jr ra; nop


def run(words, regs=None, mem_base=0x80100000, mem=None):
    code = b"".join(struct.pack("<I", w) for w in words)
    e = Emu(code, vram_base=0x80010000, file_off=0)
    buf = bytearray(mem) if mem else bytearray(0x10000)
    e.map(mem_base, buf)
    e.regs[29] = mem_base + 0xFFF0
    e.regs[31] = 0xDEADBEEF
    if regs:
        for k, v in regs.items():
            e.regs[k] = v
    e.pc = 0x80010000
    for _ in range(200):
        if e.pc == 0xDEADBEEF:
            break
        e.step()
    else:
        raise AssertionError("did not return")
    return e


# ALU + moves
e = run([0x24080005, 0x24090007, 0x01095021] + RET)
assert e.regs[10] == 12, hex(e.regs[10])
# branch taken: delay executes, target skipped (beq +2 lands past addiu63)
e = run([0x10000002, 0x2408002A, 0x24080063, 0x24080007] + RET)
assert e.regs[8] == 7, hex(e.regs[8])
# branch not taken: falls through
e = run([0x14080002, 0x2408002A, 0x24080063, 0x24080007] + RET, regs={8: 1})
assert e.regs[8] == 7, hex(e.regs[8])
# 'b' alias (unconditional)
e = run([0x10000002, 0x2408002A, 0x24080063, 0x24080007] + RET)
assert e.regs[8] == 7

# lwl/lwr pair == unaligned lw for offsets 0..7 ($a0 base, $t0 target)
pat = bytes(range(32))
for off in range(8):
    e = run([0x88880000 | (off + 3), 0x98880000 | off] + RET,
            regs={4: 0x80100000}, mem=pat)
    exp = struct.unpack("<I", pat[off:off + 4])[0]
    assert e.regs[8] == exp, (off, hex(e.regs[8]), hex(exp))
# merge behavior: low bits preserved by lwl, high by lwr
e = run([0x3C08DEAD, 0x3508BEEF, 0x88880003, 0x98880000] + RET,
        regs={4: 0x80100000}, mem=pat)
assert e.regs[8] == struct.unpack("<I", pat[0:4])[0], hex(e.regs[8])

# swl/swr pair == unaligned sw for offsets 0..3 ($a0 base, $t1 value)
for off in range(4):
    e = run([0xA8890000 | (off + 3), 0xB8890000 | off] + RET,
            regs={4: 0x80100000, 9: 0xA5A5A5A5}, mem=bytes(32))
    got = bytes(e.segs[1][1][off:off + 4])
    assert got == b"\xa5\xa5\xa5\xa5", (off, got.hex())

# jal/jr round trip with return value (outer saves ra on stack)
e = run([0x27BDFFF8, 0xAFBF0004, 0x0C004008, 0x00000000, 0x8FBF0004,
         0x27BD0008, 0x03E00008, 0x00000000, 0x2402002A, 0x03E00008,
         0x00000000])
assert e.regs[2] == 42, e.regs[2]

# lui/addiu sign-extends (0xE7C0 = -6208): 0x801D0000-6208 = 0x801CE7C0
e = run([0x3C04801D, 0x2484E7C0] + RET)
assert e.regs[4] == 0x801CE7C0, hex(e.regs[4])
# lui/ori zero-extends: 0x801D0000|0xE7C0 = 0x801DE7C0
e = run([0x3C04801D, 0x3484E7C0] + RET)
assert e.regs[4] == 0x801DE7C0, hex(e.regs[4])

print("EMU SELF-TEST PASS")
