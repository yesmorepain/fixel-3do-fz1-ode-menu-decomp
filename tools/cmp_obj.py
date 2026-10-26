#!/usr/bin/env python3
"""Compare each function of an AOF object with the same function in a 3DO AIF program (per-word diff).

Usage: python3 cmp_obj.py <aif program> <obj.o> [names.tsv ...] [-v]

Program addresses come from the names TSVs (addr<TAB>name...; default tools/names/<prog>.tsv and
tools/names/matched/<prog>.tsv). Relocated words are wildcards. Prints one line per symbol: name, address,
words, differing words; -v also disassembles the differing words (capstone, ARM BE).
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from match_libs import members, parse_aof  # noqa: E402


def main():
    args = sys.argv[1:]
    verbose = "-v" in args
    args = [a for a in args if a != "-v"]
    prog_path, obj = args[0], args[1]
    tsvs = args[2:]
    if not tsvs:
        here = os.path.dirname(os.path.abspath(__file__))
        p = os.path.basename(prog_path)
        tsvs = [os.path.join(here, "names", "matched", p + ".tsv"), os.path.join(here, "names", p + ".tsv")]
    addr = {}
    for t in tsvs:
        if os.path.exists(t):
            for line in open(t):
                f = line.rstrip("\n").split("\t")
                if len(f) >= 2 and not line.startswith("#"):
                    try:
                        addr[f[1]] = int(f[0], 16)
                    except ValueError:
                        pass
    prog = open(prog_path, "rb").read()
    md = None
    if verbose:
        from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_BIG_ENDIAN
        md = Cs(CS_ARCH_ARM, CS_MODE_ARM | CS_MODE_BIG_ENDIAN)

    def dis(w, pc):
        i = next(md.disasm(struct.pack(">I", w), pc), None)
        return f"{i.mnemonic} {i.op_str}" if i else f".word {w:#010x}"

    total = bad = 0
    for _, ch in members(obj):
        for a in parse_aof(ch):
            if not a["code"] or not a["data"]:
                continue
            syms = sorted(set(a["syms"]))
            for i, (v, n) in enumerate(syms):
                e = syms[i + 1][0] if i + 1 < len(syms) else len(a["data"])
                if n not in addr:
                    print(f"{n:28s} ------  {(e - v) // 4:4d} words  (no address)")
                    continue
                base = addr[n]
                diffs = []
                for o in range(v, e & ~3, 4):
                    w = struct.unpack_from(">I", a["data"], o)[0]
                    pa = base + o - v
                    pw = struct.unpack_from(">I", prog, pa)[0] if pa + 4 <= len(prog) else None
                    if o not in a["rel"] and w != pw:
                        diffs.append((o - v, w, pw))
                total += 1
                bad += bool(diffs)
                print(f"{n:28s} {base:6x}  {(e - v) // 4:4d} words  {len(diffs):4d} diff")
                if verbose:
                    for off, w, pw in diffs:
                        print(f"    +{off:04x}  obj {w:08x} {dis(w, off):32s} prog {pw:08x} {dis(pw, off)}")
    sys.stderr.write(f"{total - bad}/{total} functions identical\n")


if __name__ == "__main__":
    main()
