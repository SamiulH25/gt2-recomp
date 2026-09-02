#!/usr/bin/env python3
"""Quick disasm sanity via capstone"""
import pathlib, struct
try:
    import capstone
except ImportError:
    print("pip install capstone")
    exit(1)

scus = pathlib.Path("/tmp/opencode/SCUS_944.88").read_bytes()
code = scus[0x800:]
cs = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32+capstone.CS_MODE_LITTLE_ENDIAN)
def dump(va,count=40):
    off=va-0x80010000
    data=code[off:off+count*4]
    for ins in cs.disasm(data,va):
        print(f"{ins.address:08x}: {ins.mnemonic:8} {ins.op_str:20} ; {ins.bytes.hex()}")

print("=== 80010000 init ===")
dump(0x80010000,30)
print("\n=== 8005d600 entry ===")
dump(0x8005d600,30)
print("\n=== 8001146c loader stub ===")
dump(0x8001146c,40)

# OVL
ovl=pathlib.Path("/tmp/opencode/GT2.OVL.decompressed").read_bytes()
print("\n=== OVL dec 0 (guess load 80100000) ===")
for ins in cs.disasm(ovl[:80], 0x80100000):
    print(f"{ins.address:08x}: {ins.mnemonic:8} {ins.op_str}")

# GTE count already done, syscall scan
cnt=sum(1 for i in range(0,len(code)-4,4) if struct.unpack_from('<I',code,i)[0]==0xc)
gte=sum(1 for i in range(0,len(code)-4,4) if struct.unpack_from('<I',code,i)[0]>>26==0x12)
print(f"\nSCUS syscall={cnt} gte={gte}")
