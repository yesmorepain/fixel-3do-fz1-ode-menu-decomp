#!/usr/bin/env python3
"""List sections, imported libraries and imported symbols of a PowerPC PEF container (classic Mac CFM).
Usage: python3 pef_info.py <file>"""
import struct
import sys

d = open(sys.argv[1], "rb").read()
tag1, tag2, arch, fver, ts, oldd, oldi, curv, nsec, ninst, _ = struct.unpack_from(">4s4s4sIIIIIHHI", d, 0)
assert tag1 == b"Joy!" and tag2 == b"peff", "not PEF"
print(f"arch {arch.decode()} sections {nsec}")
secs = []
for i in range(nsec):
    name, addr, exsz, unp, packed, off, kind, share, align, _ = struct.unpack_from(">iIIIIIBBBB", d, 40 + 28 * i)
    secs.append((kind, off, packed, unp))
    print(f"  section {i}: kind {kind} off {off:#x} packed {packed} unpacked {unp}")
kind, off, size, _ = next(s for s in secs if s[0] == 4)
L = d[off:off + size]
(mainS, mainO, initS, initO, termS, termO, nlibs, nsyms, nrelsec, relinstoff, strtab, hashoff, hashpow, nexp) = \
    struct.unpack_from(">iIiIiIIIIIIIII", L, 0)
def s(o): return L[strtab + o:L.index(b"\0", strtab + o)].decode("latin-1")
libs = []
for i in range(nlibs):
    nm, oldv, curv, nimp, first, opts, _, _ = struct.unpack_from(">IIIIIBBH", L, 56 + 24 * i)
    libs.append((s(nm), first, nimp))
symbase = 56 + 24 * nlibs
for name, first, n in libs:
    syms = []
    for j in range(first, first + n):
        w = struct.unpack_from(">I", L, symbase + 4 * j)[0]
        syms.append(s(w & 0xFFFFFF))
    print(f"{name} ({n}): {' '.join(syms)}")
