#!/usr/bin/env python3
"""Generate the kernellib assembler objects the ode driver links with (no ARM assembler among the MPW tools).

Usage: python3 mkkernellib.py <portfolio_os kernellib/supervectors.s> <outdir>
Writes <outdir>/superdo.o and <outdir>/supervectors.o.

superdo.o: one C$$code area as found in System/Drivers/ode 0x1060-0x108c (a later layout than portfolio_os
superdo.s): an unused DCD _KernelBase, SuperDo, then the literal _KernelBase.
supervectors.o: one STUB<name> area per stub of the given supervectors.s (`mov r12,#k; b SuperDo`), all 60
(STUBAllocACS included: it is at ode 0x1614).
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkaof import CODE_RO, write_aof  # noqa: E402

SUPERDO = [0x00000000,            # DCD _KernelBase (unused)
           0xE92D4200,            # SuperDo: stmfd sp!,{r9,lr}
           0xE59F9018,            # ldr   r9,=_KernelBase
           0xE5999000,            # ldr   r9,[r9]
           0xE5D9E02A,            # ldrb  lr,[r9,#fl_MaxUserFunctions]
           0xE08CC00E,            # add   r12,r12,lr
           0xE28CC001,            # add   r12,r12,#1
           0xE1A0E00F,            # mov   lr,pc
           0xE719F10C,            # ldr   pc,[r9,-r12,lsl #2]
           0xE8BD8200,            # ldmfd sp!,{r9,pc}
           0x00000000]            # literal _KernelBase


def arm_imm(v):
    for rot in range(16):
        x = ((v << (2 * rot)) | (v >> (32 - 2 * rot))) & 0xFFFFFFFF if rot else v
        if x < 256:
            return (rot << 8) | x
    raise ValueError(hex(v))


def mov_r12(k):
    if k >= 0:
        return 0xE3A0C000 | arm_imm(k)          # mov r12,#k
    return 0xE3E0C000 | arm_imm(~k & 0xFFFFFFFF)  # mvn r12,#~k


def parse_stubs(path):
    stubs, area = [], None
    for line in open(path):
        line = line.split(";")[0]
        m = re.match(r"\s*AREA\s+\|(\w+)\|", line)
        if m:
            area = m.group(1)
            continue
        m = re.match(r"\s*mov\s+r12\s*,\s*#(-?\d+)", line)
        if m and area:
            stubs.append((area, area[4:], int(m.group(1))))
            area = None
    return stubs


def main():
    sv, out = sys.argv[1], sys.argv[2]
    write_aof(os.path.join(out, "superdo.o"),
              [dict(name="C$$code", attr=CODE_RO, data=struct.pack(">11I", *SUPERDO),
                    relocs=[(0, "_KernelBase", "word"), (0x28, "_KernelBase", "word")])],
              [("_KernelBase", 2, 0, None), ("SuperDo", 3, 4, "C$$code")])
    areas, syms = [], [("SuperDo", 2, 0, None)]
    for area, name, k in parse_stubs(sv):
        # b SuperDo at offset 4: field = -(4+8)/4 relative to the area start, resolved by the linker
        areas.append(dict(name=area, attr=CODE_RO, data=struct.pack(">II", mov_r12(k), 0xEAFFFFFD),
                          relocs=[(4, "SuperDo", "branch")]))
        syms += [(area, 1, 0, area), (name, 3, 0, area)]
    write_aof(os.path.join(out, "supervectors.o"), areas, syms)
    print(f"superdo.o, supervectors.o ({len(areas)} stubs)")


if __name__ == "__main__":
    main()
