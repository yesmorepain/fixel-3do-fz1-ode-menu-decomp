#!/usr/bin/env python3
"""Compare an object's C$$code / C$$data / C$$zidata areas with the program at given addresses.

Usage: python3 cmp_area.py <aif program> <obj.o> <code addr> [<data addr> [<zidata addr>]]
Relocated words are wildcards. Prints size and result per area.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from match_libs import members, parse_aof  # noqa: E402


def main():
    prog = open(sys.argv[1], "rb").read()
    addrs = [int(a, 16) for a in sys.argv[3:]]
    want = dict(zip(["C$$code", "C$$data", "C$$zidata"], addrs))
    ok = True
    for _, ch in members(sys.argv[2]):
        head, strt = ch["OBJ_HEAD"], ch["OBJ_STRT"]
        sizes = {}
        for i in range(struct.unpack_from(">I", head, 8)[0]):
            nm, attr, size, _, _ = struct.unpack_from(">5I", head, 24 + 20 * i)
            sizes[strt[nm:strt.index(b"\0", nm)].decode()] = size
        for a in parse_aof(ch):
            n = a["name"]
            if n not in want:
                continue
            base = want[n]
            if n == "C$$zidata":
                print(f"{n:10s} {sizes[n]:#7x} at {base:#x}-{base + sizes[n]:#x}")
                continue
            b = a["data"]
            bad = [o for o in range(0, len(b), 4) if o not in a["rel"] and b[o:o + 4] != prog[base + o:base + o + 4]]
            ok &= not bad
            print(f"{n:10s} {len(b):#7x} at {base:#x}-{base + len(b):#x}  " +
                  ("identical" if not bad else f"{len(bad)} words differ, first +{bad[0]:#x}"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
