#!/usr/bin/env python3
"""Disassemble a PEF tool's code section word by word (capstone stops at data otherwise).
Usage: ppcdis.py <pef> [out.s]   (writes 'addr word mnemonic ops' lines; imports resolved in glue comments)"""
import os
import struct
import sys
import warnings

warnings.filterwarnings("ignore")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pef import load_pef  # noqa: E402
import capstone  # noqa: E402

d = open(sys.argv[1], "rb").read()
imp = {}


def resolve(lib, sym, cls):
    a = 0x30000000 + 8 * len(imp)
    imp[a] = sym
    return a


p = load_pef(d, {0: 0x10000000, 1: 0x20000000}, resolve)
code, data = p["mem"][0], p["mem"][1]
md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
toc = struct.unpack_from(">I", data, p["main"] - 0x20000000 + 4)[0]
out = open(sys.argv[2], "w") if len(sys.argv) > 2 else sys.stdout
for o in range(0, len(code), 4):
    w = struct.unpack_from(">I", code, o)[0]
    a = 0x10000000 + o
    r = list(md.disasm(code[o:o + 4], a, 1))
    txt = f"{r[0].mnemonic} {r[0].op_str}" if r else ".word"
    if r and r[0].mnemonic == "lwz" and r[0].op_str.startswith("r12, ") and "(r2)" in r[0].op_str:
        off = int(r[0].op_str.split(", ")[1].split("(")[0], 0)
        v = struct.unpack_from(">I", data, toc + off - 0x20000000)[0]
        if v in imp:
            txt += f"    ; glue -> {imp[v]}"
    out.write(f"{a:08x} {w:08x} {txt}\n")
