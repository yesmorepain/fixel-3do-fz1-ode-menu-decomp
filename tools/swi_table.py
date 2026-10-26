#!/usr/bin/env python3
"""Build a Portfolio SWI number -> name table from 3DO SDK headers (__swi(BASE+N) Name(...)).
Usage: python3 swi_table.py <sdk include dir> > swi_table.tsv"""
import os
import re
import sys

root = sys.argv[1]
defs, decls, local = {}, [], {}
for dp, _, fs in os.walk(root):
    for f in fs:
        if not f.endswith(".h"):
            continue
        t = open(os.path.join(dp, f), encoding="latin-1").read()
        for m in re.finditer(r"#define[ \t]+(\w+)[ \t]+([^\n/]+)", t):
            defs.setdefault(m.group(1), m.group(2).strip())
            local.setdefault(f, {}).setdefault(m.group(1), m.group(2).strip())
        for m in re.finditer(r"__swi\s*\(([^)]*\)?)\)\s*[\w\s\*]*?\b(\w+)\s*\(", t):
            decls.append((m.group(1), m.group(2), f))


def ev(expr, depth=0, f=None):
    d = dict(defs, **local.get(f, {}))  # the declaring header's own defines win
    expr = re.sub(r"\b([A-Za-z_]\w*)\b", lambda m: "(" + str(ev(d[m.group(1)], depth + 1, f)) + ")"
                  if m.group(1) in d and depth < 8 else m.group(1), expr)
    return eval(expr, {"__builtins__": {}})


out = {0x11: ("Exit", "AIF header: SWI after BL entry"),  # not in the headers
       0x40023: ("AudioSwi35", "AUDIOSWI+35, used by audio.lib SuperSetAudioFolioInfo; no header name")}
for expr, name, f in decls:
    try:
        n = ev(expr, 0, f)
    except Exception:
        continue
    out.setdefault(n, (name, f))
for n in sorted(out):
    print(f"{n:#x}\t{out[n][0]}\t{out[n][1]}")
