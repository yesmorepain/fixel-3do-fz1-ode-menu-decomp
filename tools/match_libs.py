#!/usr/bin/env python3
"""Byte-match Norcroft ARM library code (ALF .lib / AOF .o, big-endian chunk files) against a 3DO AIF program.

Usage: python3 match_libs.py <aif program> <lib or .o> [...]  [--tsv out.tsv] [--min N]

Each code area of each AOF member is cut at its defined symbols; every piece is searched in the program's RO
region at word-aligned positions. Words covered by a relocation are wildcards (PC-relative BLs to the same
area carry no relocation and must match exactly). A piece is reported when it matches exactly one place.
Whole areas are matched too, which also locates static functions (reported as <member>+<offset>).
Output TSV: addr, name, library(member), words, kind.
"""
import re
import struct
import sys
from collections import defaultdict

CHUNK_MAGIC = 0xC3CBC6C5


def chunks(data):
    magic, maxc, numc = struct.unpack_from(">III", data, 0)
    if magic != CHUNK_MAGIC:
        raise ValueError("not a big-endian chunk file")
    out = []
    for i in range(maxc):
        cid, off, size = struct.unpack_from(">8sII", data, 12 + 16 * i)
        if off:
            out.append((cid.decode("latin-1"), data[off:off + size]))
    return out


def members(path):
    data = open(path, "rb").read()
    ch = chunks(data)
    ids = [c[0] for c in ch]
    if "OBJ_HEAD" in ids:
        return [(path.split("/")[-1], dict(ch))]
    # ALF: LIB_DIRY entries point at chunk indices of LIB_DATA members.
    allc = []
    magic, maxc, numc = struct.unpack_from(">III", data, 0)
    for i in range(maxc):
        cid, off, size = struct.unpack_from(">8sII", data, 12 + 16 * i)
        allc.append((cid.decode("latin-1"), data[off:off + size] if off else b""))
    diry = next(c for i, c in allc if i == "LIB_DIRY")
    res, o = [], 0
    while o + 12 <= len(diry):
        idx, elen, dlen = struct.unpack_from(">III", diry, o)
        if elen == 0:
            break
        if idx:
            name = diry[o + 12:o + 12 + dlen].split(b"\0")[0].decode("latin-1")
            res.append((name, dict(chunks(allc[idx][1]))))
        o += elen
    return res


def parse_aof(ch):
    head, area, strt, symt = ch["OBJ_HEAD"], ch["OBJ_AREA"], ch.get("OBJ_STRT", b""), ch.get("OBJ_SYMT", b"")
    typ, ver, nareas, nsyms = struct.unpack_from(">IIII", head, 0)

    def s(off):
        return strt[off:strt.index(b"\0", off)].decode("latin-1")

    areas, pos = [], 0
    for i in range(nareas):
        name, attr, size, nrel, _ = struct.unpack_from(">IIIII", head, 24 + 20 * i)
        zero = attr & 0x1000
        body = b"" if zero else area[pos:pos + size]
        pos += 0 if zero else size
        rel, relsym = set(), {}
        for r in range(nrel):
            off, fl = struct.unpack_from(">II", area, pos + 8 * r)
            rel.add(off & ~3)
            if fl & 0x80000000 and fl & 0x08000000:     # type 2, symbol-based
                relsym[off & ~3] = fl & 0xFFFFFF
            elif fl & 0x80000000:                       # type 2, area-based: area index
                relsym[off & ~3] = ("area", fl & 0xFFFFFF)
            elif not fl & 0x80000000 and fl & 0x80000:  # type 1, symbol-based
                relsym[off & ~3] = fl & 0xFFFF
        pos += 8 * nrel
        areas.append({"name": s(name), "code": bool(attr & 0x200), "data": body, "rel": rel, "relsym": relsym, "syms": []})
    byname = {a["name"]: a for a in areas}
    for i in range(nsyms):
        name, attr, val, aname = struct.unpack_from(">IIII", symt, 16 * i)
        if attr & 1 and not attr & 4 and aname in range(len(strt)):
            a = byname.get(s(aname))
            if a is not None and s(name) not in byname and not s(name).startswith("x$"):  # skip area-name symbols (ASMCODE, C$$code)
                a["syms"].append((val, s(name)))
    symnames = [s(struct.unpack_from(">I", symt, 16 * i)[0]) for i in range(nsyms)]
    for a in areas:
        a["relsym"] = {o: (f"{areas[i[1]]['name']}" if isinstance(i, tuple) and i[1] < nareas else
                           symnames[i] if not isinstance(i, tuple) and i < nsyms else None)
                       for o, i in a["relsym"].items()}
    return areas


def is_vec_stub(d):
    """ldr rX,[pc,#4]; ldr rX,[rX,#K]; ldr pc,[rX,#-N]; .word Base  -> (K, N) or None"""
    if len(d) != 16:
        return None
    w = struct.unpack(">4I", d)
    if (w[0] & 0xFFFF0FFF) == 0xE59F0004 and (w[1] & 0xFFF00000) == 0xE5900000 and (w[2] & 0xFFF0F000) == 0xE510F000:
        return (w[1] & 0xFFF, w[2] & 0xFFF)
    return None


def pattern(a, start, end):
    end = min(end, len(a["data"])) & ~3
    return [None if o in a["rel"] else struct.unpack_from(">I", a["data"], o)[0] for o in range(start, end, 4)]


def main():
    args = sys.argv[1:]
    tsv, minw = None, 4
    if "--tsv" in args:
        i = args.index("--tsv"); tsv = args[i + 1]; del args[i:i + 2]
    if "--min" in args:
        i = args.index("--min"); minw = int(args[i + 1]); del args[i:i + 2]
    prog = open(args[0], "rb").read()
    ro = struct.unpack_from(">I", prog, 0x14)[0]
    words = [struct.unpack_from(">I", prog, o)[0] for o in range(0, ro, 4)]
    index = defaultdict(list)
    for i, w in enumerate(words):
        index[w].append(i)

    def find(pat):
        k = next((j for j, w in enumerate(pat) if w is not None), None)
        if k is None:
            return []
        hits = []
        for p in index.get(pat[k], ()):
            b = p - k
            if b < 0 or b + len(pat) > len(words):
                continue
            if all(w is None or words[b + j] == w for j, w in enumerate(pat)):
                hits.append(b * 4)
        return hits

    found = {}
    vec = defaultdict(dict)          # base symbol -> {N: name}
    for lib in args[1:]:
        lname = lib.split("/")[-1]
        for mname, ch in members(lib):
            try:
                areas = parse_aof(ch)
            except Exception:
                continue
            if "builddate" in mname:
                continue
            byname_all = {x["name"] for x in areas}
            for a in areas:
                if not a["code"] or not a["data"]:
                    continue
                kn = is_vec_stub(a["data"])
                if kn is not None and a["relsym"].get(12) and a["syms"]:
                    k, n = kn
                    base = a["relsym"][12]
                    if base not in byname_all:
                        pass
                    else:
                        base = f"{lname}({mname}):{base}"
                    vec[(base, k)].setdefault(n, (a["syms"][0][1], f"{lname}({mname})"))
                    continue
                size = len(a["data"])
                whole = pattern(a, 0, size)
                wh = find(whole) if sum(w is not None for w in whole) >= minw else []
                syms = sorted(set(a["syms"]))
                if len(wh) == 1:
                    base = wh[0]
                    named = {v for v, _ in syms}
                    for v, n in syms:
                        found.setdefault(base + v, (n, f"{lname}({mname})", size // 4, "area"))
                    if 0 not in named:
                        found.setdefault(base, (mname.split("/")[-1].split(":")[-1].replace(".", "_"), f"{lname}({mname})", size // 4, "area"))
                    continue
                for i, (v, n) in enumerate(syms):
                    if v & 3:
                        continue
                    e = syms[i + 1][0] if i + 1 < len(syms) else size
                    pat = pattern(a, v, e)
                    if sum(w is not None for w in pat) < minw:
                        continue
                    h = find(pat)
                    if len(h) == 1:
                        found.setdefault(h[0], (n, f"{lname}({mname})", len(pat), "sym"))
    # Vector stubs in the program, grouped by literal (address of the folio base variable).
    stubs = defaultdict(dict)
    for i in range(len(words) - 3):
        kn = is_vec_stub(prog[4 * i:4 * i + 16])
        if kn is not None:
            stubs[(words[i + 3], kn[0])][4 * i] = kn[1]
    rw0 = ro  # cstartup's ASMdata (KernelBase) is the first RW word
    for lit, st in stubs.items():
        # pick the base whose known offsets cover most of these stubs (KernelBase for the first RW word)
        score = sorted(((sum(n in vec[b] for n in st.values()), b) for b in vec if b[1] == lit[1]), reverse=True)
        if lit == (rw0, 0) and ("KernelBase", 0) in vec:
            score = [(len(st), ("KernelBase", 0))]
        if not score or score[0][0] * 2 < len(st) or (len(score) > 1 and score[1][0] == score[0][0]):
            sys.stderr.write(f"stubs via {lit[0]:#x}+{lit[1]}: base ambiguous {score[:3]}\n")
            continue
        b = score[0][1]
        bn = b[0] if not b[1] else f"{b[0]}+{b[1]}"
        sys.stderr.write(f"stubs via {lit[0]:#x}+{lit[1]} = {bn} ({score[0][0]}/{len(st)} known)\n")
        for addr, n in st.items():
            if n not in vec[b]:
                found.setdefault(addr, (re.sub(r"\W+", "_", f"{b[0]}_vec_{n:x}"), "unknown " + bn + " vector", 4, "vec"))
                continue
            nm, l = vec[b][n]
            found[addr] = (nm, l, 4, "vec")
    rows = sorted(found.items())
    out = open(tsv, "w") if tsv else sys.stdout
    for addr, (n, l, w, k) in rows:
        out.write(f"{addr:x}\t{n}\t{l}\t{w}\t{k}\n")
    sys.stderr.write(f"{len(rows)} matches\n")


if __name__ == "__main__":
    main()
