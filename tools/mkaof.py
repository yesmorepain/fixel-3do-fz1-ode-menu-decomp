#!/usr/bin/env python3
"""Write big-endian ARM AOF (version 311) objects for the few assembler files the rebuild needs (no ARM assembler is
available among the MPW tools). Used by tools/mkkernellib.py.

Layout follows what "ARM AOF Macro Assembler 2.21" writes in the SDK 2.5 libraries (e.g. graphics.lib
supergrafdo.do / supervectors.do): chunks OBJ_HEAD, OBJ_AREA, OBJ_SYMT, OBJ_STRT, OBJ_IDFN.

areas: list of dict(name, attr, data (bytes), relocs [(offset, symname, kind)])
  kind 'word'   -> type-2 symbol word relocation               0x8a000000 | symidx
  kind 'branch' -> type-2 symbol PC-relative instruction (B/BL) 0x8f000000 | symidx
syms: list of (name, attr, value, areaname or None); attr 1 = local defined, 3 = global defined, 2 = import.
"""
import struct

CHUNK_MAGIC = 0xC3CBC6C5
AOF_TYPE = 0xC5E2D080
CODE_RO = 0x00012202        # code, read-only, 4-byte alignment (as armasm 2.21 emits for CODE, READONLY)
IDFN_ASM = b"ARM AOF Macro Assembler 2.21 (A.R.M.)\0\0\0"
RELOC = {"word": 0x8a000000, "branch": 0x8f000000}


def write_aof(path, areas, syms, idfn=IDFN_ASM):
    strt = bytearray(4)
    stroff = {}

    def s(name):
        if name not in stroff:
            stroff[name] = len(strt)
            strt.extend(name.encode() + b"\0")
        return stroff[name]

    symidx = {n: i for i, (n, _, _, _) in enumerate(syms)}
    head = bytearray(struct.pack(">6I", AOF_TYPE, 311, len(areas), len(syms), 0, 0))
    body = bytearray()
    for a in areas:
        head += struct.pack(">5I", s(a["name"]), a["attr"], len(a["data"]), len(a["relocs"]), 0)
        body += a["data"]
        for off, sym, kind in a["relocs"]:
            body += struct.pack(">II", off, RELOC[kind] | symidx[sym])
    symt = bytearray()
    for n, attr, val, an in syms:
        symt += struct.pack(">4I", s(n), attr, val, s(an) if an is not None else 0)
    while len(strt) % 4:
        strt.append(0)
    struct.pack_into(">I", strt, 0, len(strt))
    chunks = [(b"OBJ_HEAD", bytes(head)), (b"OBJ_AREA", bytes(body)), (b"OBJ_SYMT", bytes(symt)),
              (b"OBJ_STRT", bytes(strt)), (b"OBJ_IDFN", idfn)]
    off = 12 + 16 * len(chunks)
    out = bytearray(struct.pack(">3I", CHUNK_MAGIC, len(chunks), len(chunks)))
    data = bytearray()
    for cid, c in chunks:
        out += struct.pack(">8sII", cid, off + len(data), len(c))
        data += c
        while len(data) % 4:
            data.append(0)
    open(path, "wb").write(bytes(out + data))
