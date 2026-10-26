#!/usr/bin/env python3
"""Library names from a byte-identical rebuild: armlink -map output + -symbols file -> names TSV.

Usage: linkmap_names.py <prog.map> <prog.sym> --own a.o,b.o [--include dir] [--tsv out.tsv]
Every global symbol inside a CODE area of a non-own object (SDK libraries, cstartup.o, original SDK/OS sources) is
written as addr, name, object, area size in words, "link". Own objects (reconstructed src/*.c) are skipped: their
names are ours, not the original ones. Area-name symbols (ASMCODE, C$$code, x$...) and $$ symbols are skipped.
Several symbols at one address: the name declared in the SDK headers (--include) wins, else the shortest; the others
go into the object column. Addresses below 0x100 (AIF header, 3DO binary header in cstartup's ASMCODE) are data, skipped.
"""
import os
import re
import sys
from collections import defaultdict


def main():
    args = sys.argv[1:]
    tsv, own = None, set()
    if "--tsv" in args:
        i = args.index("--tsv"); tsv = args[i + 1]; del args[i:i + 2]
    declared = set()
    if "--include" in args:
        i = args.index("--include")
        for d, _, fs in os.walk(args[i + 1]):
            for f in fs:
                declared |= set(re.findall(r"\w+", open(os.path.join(d, f), encoding="latin-1").read()))
        del args[i:i + 2]
    if "--own" in args:
        i = args.index("--own"); own = set(args[i + 1].split(",")); del args[i:i + 2]
    areas, areanames = [], set()
    for l in open(args[0]):
        m = re.match(r"([0-9a-f]+)\s+([0-9a-f]+)\s+(\w+)\s+(\S+) from object file (\S+)", l)
        if m:
            obj = m.group(5).split("/")[-1]
            areanames.add(m.group(4))
            if m.group(3) == "CODE" and obj not in own:
                areas.append((int(m.group(1), 16), int(m.group(2), 16), obj))
    syms = defaultdict(set)
    for l in open(args[1]):
        p = l.split()
        if len(p) == 2 and re.fullmatch(r"[0-9a-f]+", p[1]) and "$$" not in p[0] and p[0] not in areanames \
                and not p[0].startswith("x$"):
            syms[int(p[1], 16)].add(p[0])
    out = []
    for a in sorted(syms):
        hit = next(((b, s, o) for b, s, o in areas if b <= a < b + s), None)
        if hit is None or a < 0x100:
            continue
        names = sorted(syms[a], key=lambda n: (n not in declared, len(n), n))
        alias = f" (= {', '.join(names[1:])})" if len(names) > 1 else ""
        out.append(f"{a:x}\t{names[0]}\t{hit[2]}{alias}\t{hit[1] // 4}\tlink\n")
    if tsv:
        open(tsv, "w").writelines(out)
    else:
        sys.stdout.writelines(out)
    sys.stderr.write(f"{len(out)} names\n")


if __name__ == "__main__":
    main()
