#!/usr/bin/env python3
"""Minimal MIPS-I interpreter for static RE experiments.

Decodes with capstone, interprets a pragmatic subset (integer ALU, loads/
stores incl. lwl/lwr/swl/swr, branches/jumps with delay slots). No COP0/
COP2/FPU/traps: aborts loudly if hit, so experiments stay honest.

Usage:
    from mips_emu import Emu
    emu = Emu(scus_bytes, vram_base=0x80010000, file_off=0x800)
    emu.map(0x800A0000, bytearray(0x100000))   # extra RAM
    emu.write_word(0x801FFF00 - 4, 0)
    ret = emu.call(0x800100E4, a0=addr_of_path, a1=0)
"""
import struct

import capstone

_VALID_MNEMS = set(
    """lui aui ori andi xori addiu addi addu add subu sub sll srl sra sllv
    srlv srav and or xor nor slt slti sltiu sltu mult multu div divu mfhi
    mflo mtlo mthi lw lh lhu lb lbu sw sh sb lwl lwr swl swr
    beq bne blez bgtz bltz bgez bltzal bgezal beqz bnez j jal jr jalr
    jalr.hb movz movn nop break syscall""".split()
)


class Trap(Exception):
    pass


class Emu:
    def __init__(self, scus_bytes, vram_base=0x80010000, file_off=0x800):
        self.cs = capstone.Cs(capstone.CS_ARCH_MIPS,
                              capstone.CS_MODE_MIPS32 + capstone.CS_MODE_LITTLE_ENDIAN)
        self.cs.detail = True
        # (start, bytearray) segments; SCUS text first
        self.segs = []
        self.map(vram_base, bytearray(scus_bytes), readonly=False,
                 file_base=file_off, vram_base=vram_base)
        self.regs = [0] * 32
        self.hi = self.lo = 0
        self.pc = 0
        self.steps = 0
        self.trace = None  # set to list to record (pc, mnem, op)
        self._reg_names = {f"${i}": i for i in range(32)}
        self._reg_names.update({
            "$zero": 0, "$at": 1, "$v0": 2, "$v1": 3,
            "$a0": 4, "$a1": 5, "$a2": 6, "$a3": 7,
            "$t0": 8, "$t1": 9, "$t2": 10, "$t3": 11,
            "$t4": 12, "$t5": 13, "$t6": 14, "$t7": 15,
            "$s0": 16, "$s1": 17, "$s2": 18, "$s3": 19,
            "$s4": 20, "$s5": 21, "$s6": 22, "$s7": 23,
            "$t8": 24, "$t9": 25, "$k0": 26, "$k1": 27,
            "$gp": 28, "$sp": 29, "$fp": 30, "$ra": 31,
        })

    def map(self, start, buf, readonly=False, file_base=None, vram_base=None):
        self.segs.append([start, buf, readonly])

    def _seg(self, addr):
        # Later mappings shadow earlier ones (RAM over file images).
        for start, buf, _ in reversed(self.segs):
            if start <= addr < start + len(buf):
                return start, buf
        raise Trap(f"unmapped addr {addr:#x} at pc {self.pc:#x}")

    def read(self, addr, n):
        start, buf = self._seg(addr)
        off = addr - start
        if off + n > len(buf):
            raise Trap(f"read past segment {addr:#x}")
        return bytes(buf[off:off + n])

    def write(self, addr, data):
        start, buf = self._seg(addr)
        off = addr - start
        if off + len(data) > len(buf):
            raise Trap(f"write past segment {addr:#x}")
        buf[off:off + len(data)] = data

    def read_word(self, addr):
        return struct.unpack("<I", self.read(addr, 4))[0]

    def write_word(self, addr, v):
        self.write(addr, struct.pack("<I", v & 0xFFFFFFFF))

    def write_cstr(self, addr, s):
        self.write(addr, s.encode() + b"\x00")

    def read_cstr(self, addr, limit=256):
        out = bytearray()
        for _ in range(limit):
            b = self.read(addr, 1)[0]
            if b == 0:
                break
            out.append(b)
            addr += 1
        return bytes(out)
        out = bytearray()
        for _ in range(limit):
            b = self.read(addr, 1)[0]
            if b == 0:
                break
            out.append(b)
            addr += 1
        return bytes(out)

    # -- operand helpers -------------------------------------------------
    def _r(self, name):
        return self._reg_names[name.strip().lower()]

    @staticmethod
    def _s16(v):
        v &= 0xFFFF
        return v - 0x10000 if v & 0x8000 else v

    @staticmethod
    def _s32(v):
        v &= 0xFFFFFFFF
        return v - 0x100000000 if v & 0x80000000 else v

    @staticmethod
    def _u(v):
        return v & 0xFFFFFFFF

    def _mem_addr(self, op, simm):
        # forms: "0x10($sp)" / "($s0)" / "-0x6c18($v0)"; offset taken from
        # the raw encoding (see _exec), op_str gives only the register.
        op = op.strip()
        assert op.endswith(")")
        _, reg_s = op[:-1].split("(")
        return (self.regs[self._r(reg_s)] + simm) & 0xFFFFFFFF

    # -- core ------------------------------------------------------------
    def _fetch(self, pc):
        raw = self.read(pc, 4)
        ins = next(self.cs.disasm(raw, pc), None)
        if ins is None:
            raise Trap(f"bad insn at {pc:#x}")
        return ins

    def _exec(self, ins, pc):
        """Execute one instruction. Returns next-pc (ignoring delay rules)."""
        m, ops = ins.mnemonic, [o.strip() for o in ins.op_str.split(",")]
        R = self.regs
        nxt = (pc + 4) & 0xFFFFFFFF
        # Immediates/shamt decoded from the raw word: capstone's pretty
        # printer occasionally mangles signed hex (e.g. 0xE7C0 as -0x1840).
        word = struct.unpack("<I", ins.bytes)[0]
        uimm = word & 0xFFFF
        simm = uimm - 0x10000 if uimm & 0x8000 else uimm
        shamt = (word >> 6) & 0x1F
        # pseudo-mnemonic normalization (capstone prints absolute targets)
        if m == "b":
            m, ops = "beq", ["$zero", "$zero"] + ops
        elif m == "bal":
            m, ops = "bgezal", ["$zero"] + ops
        elif m == "move":
            m, ops = "addu", [ops[0], ops[1], "$zero"]
        elif m in ("neg", "negu"):
            m, ops = "subu", [ops[0], "$zero", ops[1]]
        elif m == "li":
            m, ops = "addiu", [ops[0], "$zero", ops[1]]
        elif m == "nop":
            return nxt

        def wreg(i, v):
            if i != 0:
                R[i] = v & 0xFFFFFFFF

        if m == "lui":
            wreg(self._r(ops[0]), uimm << 16)
        elif m in ("ori", "xori", "andi"):
            a = R[self._r(ops[1])]
            wreg(self._r(ops[0]), a | uimm if m == "ori" else a ^ uimm if m == "xori" else a & uimm)
        elif m in ("addiu", "addi", "slti", "sltiu"):
            a = R[self._r(ops[1])]
            if m == "addiu" or m == "addi":
                wreg(self._r(ops[0]), a + simm)
            elif m == "slti":
                wreg(self._r(ops[0]), 1 if self._s32(a) < simm else 0)
            else:
                wreg(self._r(ops[0]), 1 if a < (simm & 0xFFFFFFFF) else 0)
        elif m in ("addu", "add", "subu", "sub", "and", "or", "xor", "nor",
                   "slt", "sltu"):
            a, b = R[self._r(ops[1])], R[self._r(ops[2])]
            if m in ("addu", "add"):
                wreg(self._r(ops[0]), a + b)
            elif m in ("subu", "sub"):
                wreg(self._r(ops[0]), a - b)
            elif m == "and":
                wreg(self._r(ops[0]), a & b)
            elif m == "or":
                wreg(self._r(ops[0]), a | b)
            elif m == "xor":
                wreg(self._r(ops[0]), a ^ b)
            elif m == "nor":
                wreg(self._r(ops[0]), ~(a | b))
            elif m == "slt":
                wreg(self._r(ops[0]), 1 if self._s32(a) < self._s32(b) else 0)
            else:
                wreg(self._r(ops[0]), 1 if a < b else 0)
        elif m in ("sll", "srl", "sra"):
            sh = shamt
            a = R[self._r(ops[1])]
            if m == "sll":
                wreg(self._r(ops[0]), a << sh)
            elif m == "srl":
                wreg(self._r(ops[0]), a >> sh)
            else:
                wreg(self._r(ops[0]), self._s32(a) >> sh)
        elif m in ("sllv", "srlv", "srav"):
            sh = R[self._r(ops[2])] & 0x1F
            a = R[self._r(ops[1])]
            if m == "sllv":
                wreg(self._r(ops[0]), a << sh)
            elif m == "srlv":
                wreg(self._r(ops[0]), a >> sh)
            else:
                wreg(self._r(ops[0]), self._s32(a) >> sh)
        elif m in ("lw", "lh", "lhu", "lb", "lbu"):
            addr = self._mem_addr(ops[1], simm)
            if m == "lw":
                wreg(self._r(ops[0]), struct.unpack("<I", self.read(addr, 4))[0])
            elif m == "lh":
                wreg(self._r(ops[0]), self._s16(struct.unpack("<H", self.read(addr, 2))[0]))
            elif m == "lhu":
                wreg(self._r(ops[0]), struct.unpack("<H", self.read(addr, 2))[0])
            elif m == "lb":
                v = self.read(addr, 1)[0]
                wreg(self._r(ops[0]), v - 0x100 if v & 0x80 else v)
            else:
                wreg(self._r(ops[0]), self.read(addr, 1)[0])
        elif m in ("sw", "sh", "sb"):
            addr = self._mem_addr(ops[1], simm)
            v = R[self._r(ops[0])]
            if m == "sw":
                self.write(addr, struct.pack("<I", v))
            elif m == "sh":
                self.write(addr, struct.pack("<H", v & 0xFFFF))
            else:
                self.write(addr, bytes([v & 0xFF]))
        elif m in ("lwl", "lwr", "swl", "swr"):
            addr = self._mem_addr(ops[1], simm)
            n = addr & 3
            if m == "lwl":
                w = struct.unpack("<I", self.read(addr & ~3, 4))[0]
                k = n + 1
                keep = (1 << (32 - 8 * k)) - 1 if k < 4 else 0
                wreg(self._r(ops[0]), (R[self._r(ops[0])] & keep) | (w << (32 - 8 * k)))
            elif m == "lwr":
                w = struct.unpack("<I", self.read(addr & ~3, 4))[0]
                k = 4 - n
                mask = (1 << (8 * k)) - 1
                wreg(self._r(ops[0]), (R[self._r(ops[0])] & ~mask) | ((w >> (8 * n)) & mask))
            elif m == "swl":
                v = R[self._r(ops[0])]
                for j in range(n + 1):
                    self.write(addr - j, bytes([(v >> (24 - 8 * j)) & 0xFF]))
            else:  # swr
                v = R[self._r(ops[0])]
                for j in range(4 - n):
                    self.write(addr + j, bytes([(v >> (8 * j)) & 0xFF]))
        elif m in ("mult", "multu"):
            a, b = R[self._r(ops[0])], R[self._r(ops[1])]
            if m == "mult":
                p = self._s32(a) * self._s32(b)
            else:
                p = a * b
            p &= 0xFFFFFFFFFFFFFFFF
            self.lo, self.hi = p & 0xFFFFFFFF, (p >> 32) & 0xFFFFFFFF
        elif m in ("div", "divu"):
            # capstone renders the dummy rd ($zero) first: operands are
            # (rd, rs, rt); divide the LAST two (found 2026-09-09: the old
            # code divided R[$zero]/R[rs], trapping spuriously whenever
            # R[rs] was 0 and miscomputing otherwise).
            a, b = R[self._r(ops[-2])], R[self._r(ops[-1])]
            if b == 0:
                raise Trap("div by zero")
            if m == "div":
                q = abs(self._s32(a)) // abs(self._s32(b))
                if (self._s32(a) < 0) != (self._s32(b) < 0):
                    q = -q
                r = self._s32(a) - q * self._s32(b)
                self.lo, self.hi = q & 0xFFFFFFFF, r & 0xFFFFFFFF
            else:
                self.lo, self.hi = a // b, a % b
        elif m == "mfhi":
            wreg(self._r(ops[0]), self.hi)
        elif m == "mflo":
            wreg(self._r(ops[0]), self.lo)
        elif m in ("mtlo", "mthi"):
            (setattr(self, "lo", R[self._r(ops[0])]) if m == "mtlo"
             else setattr(self, "hi", R[self._r(ops[0])]))
        elif m in ("movz", "movn"):
            if (R[self._r(ops[2])] == 0) == (m == "movz"):
                wreg(self._r(ops[0]), R[self._r(ops[1])])
        elif m in ("j", "jal", "jr", "jalr"):
            # capstone renders j/jal targets absolute; jr/jalr use registers
            if m == "j":
                nxt = int(ops[0], 0) & 0xFFFFFFFF
            elif m == "jal":
                R[31] = (pc + 8) & 0xFFFFFFFF
                nxt = int(ops[0], 0) & 0xFFFFFFFF
            elif m == "jr":
                nxt = R[self._r(ops[0])]
            else:
                dst = R[self._r(ops[1] if len(ops) > 1 else ops[0])]
                link = self._r(ops[0]) if len(ops) > 1 else 31
                wreg(link, (pc + 8) & 0xFFFFFFFF)
                nxt = dst
            nxt &= 0xFFFFFFFF
        elif m in ("beq", "bne", "blez", "bgtz", "bltz", "bgez",
                   "beqz", "bnez", "bltzal", "bgezal"):
            # capstone renders branch targets as absolute addresses
            tgt = int(ops[-1], 0) & 0xFFFFFFFF
            nxt = self._exec_branch(m, ops, pc, tgt,
                                    link=m in ("bltzal", "bgezal"))
        elif m in ("nop", "break"):
            pass
        elif m == "syscall":
            raise Trap(f"syscall at {pc:#x}")
        else:
            raise Trap(f"unimplemented {m} {ins.op_str} at {pc:#x}")
        return nxt

    _JUMPS = {"j", "jal", "jr", "jalr", "beq", "bne", "blez", "bgtz",
              "bltz", "bgez", "beqz", "bnez", "bltzal", "bgezal"}
    _LIKELY = {"beql", "bnel", "blezl", "bgtzl", "bltzl", "bgezl",
               "beqzl", "bnezl"}

    def step(self):
        ins = self._fetch(self.pc)
        if self.trace is not None and len(self.trace) < 100000:
            self.trace.append((self.pc, ins.mnemonic, ins.op_str))
        if ins.mnemonic in self._LIKELY:
            # likely branch: delay slot executes only if taken
            tgt = int(ins.op_str.split(",")[-1].strip(), 0) & 0xFFFFFFFF
            # evaluate condition via a synthetic non-likely exec
            fake = {"beql": "beq", "bnel": "bne", "blezl": "blez",
                    "bgtzl": "bgtz", "bltzl": "bltz", "bgezl": "bgez",
                    "beqzl": "beqz", "bnezl": "bnez"}[ins.mnemonic]
            nxt = self._exec_branch(fake, [o.strip() for o in ins.op_str.split(",")],
                                    self.pc, tgt, link=False)
            if nxt == tgt:
                self._exec(self._fetch((self.pc + 4) & 0xFFFFFFFF), self.pc + 4)
            self.pc = nxt
        elif ins.mnemonic in self._JUMPS:
            delay = self._fetch((self.pc + 4) & 0xFFFFFFFF)
            if delay.mnemonic in self._JUMPS or delay.mnemonic in self._LIKELY:
                raise Trap(f"jump in delay slot at {self.pc:#x}")
            nxt = self._exec(ins, self.pc)
            self._exec(delay, self.pc + 4)  # result of delay discarded for pc
            self.pc = nxt
        else:
            self.pc = self._exec(ins, self.pc)
        self.steps += 1

    def _exec_branch(self, m, ops, pc, tgt, link):
        R = self.regs
        if m in ("beq", "beqz"):
            a = R[self._r(ops[0])]
            b = R[self._r(ops[1])] if m == "beq" else 0
            take = a == b
        elif m in ("bne", "bnez"):
            a = R[self._r(ops[0])]
            b = R[self._r(ops[1])] if m == "bne" else 0
            take = a != b
        elif m == "blez":
            take = self._s32(R[self._r(ops[0])]) <= 0
        elif m == "bgtz":
            take = self._s32(R[self._r(ops[0])]) > 0
        elif m in ("bltz", "bltzal"):
            take = self._s32(R[self._r(ops[0])]) < 0
        else:
            take = self._s32(R[self._r(ops[0])]) >= 0
        if link:
            R[31] = (pc + 8) & 0xFFFFFFFF
        return tgt if take else (pc + 8) & 0xFFFFFFFF

    def call(self, entry, a0=0, a1=0, a2=0, a3=0, sp=0x801FFF00, limit=2000000):
        self.regs = [0] * 32
        self.regs[4], self.regs[5], self.regs[6], self.regs[7] = a0, a1, a2, a3
        self.regs[29] = sp
        self.regs[31] = 0xDEADBEEF
        self.pc = entry
        self.steps = 0
        hooks = getattr(self, "hooks", {})
        while self.steps < limit:
            if self.pc == 0xDEADBEEF:
                return self.regs[2]
            if self.pc in hooks:
                hooks[self.pc](self)
                continue
            self.step()
        raise Trap(f"step limit {limit} exceeded (pc={self.pc:#x})")
