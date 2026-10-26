#!/usr/bin/env python3
"""Minimal PEF (classic Mac OS Code Fragment Manager) loader for PowerPC MPW tools.

load_pef(data, bases) -> dict with section bytearrays, base addresses, imports and the main TVector address.
Relocations are applied with every imported symbol pointing at an address chosen by the caller (resolve(lib, sym,
cls) -> address). Format: Apple "Mac OS Runtime Architectures", chapter 8 (PEF).
"""
import struct


def _pidata(src, size):
    """Expand a pattern-initialized data section."""
    out = bytearray()
    i = 0

    def arg():
        nonlocal i
        v = 0
        while True:
            b = src[i]; i += 1
            v = (v << 7) | (b & 0x7F)
            if not b & 0x80:
                return v

    while i < len(src):
        b = src[i]; i += 1
        op, cnt = b >> 5, b & 0x1F
        if cnt == 0:
            cnt = arg()
        if op == 0:                                   # zero
            out += bytes(cnt)
        elif op == 1:                                 # block copy
            out += src[i:i + cnt]; i += cnt
        elif op == 2:                                 # repeated block
            rep = arg() + 1
            blk = src[i:i + cnt]; i += cnt
            out += blk * rep
        elif op == 3:                                 # interleave repeat block with block copy
            custom, rep = arg(), arg()
            common = src[i:i + cnt]; i += cnt
            for _ in range(rep):
                out += common + src[i:i + custom]; i += custom
            out += common
        elif op == 4:                                 # interleave repeat block with zero
            custom, rep = arg(), arg()
            for _ in range(rep):
                out += bytes(cnt) + src[i:i + custom]; i += custom
            out += bytes(cnt)
        else:
            raise ValueError(f"bad pidata opcode {op}")
    out += bytes(max(0, size - len(out)))
    return out


def load_pef(d, bases, resolve):
    assert d[0:8] == b"Joy!peff" and d[8:12] == b"pwpc"
    nsec = struct.unpack_from(">H", d, 32)[0]
    secs = []
    for i in range(nsec):
        name, addr, exsz, unp, packed, off, kind, share, align, _ = struct.unpack_from(">iIIIIIBBBB", d, 40 + 28 * i)
        secs.append({"kind": kind, "off": off, "packed": packed, "unpacked": unp, "total": exsz})
    mem = {}
    for i, s in enumerate(secs):
        if s["kind"] == 4:
            continue
        raw = d[s["off"]:s["off"] + s["packed"]]
        if s["kind"] == 2:
            body = _pidata(raw, s["total"])
        else:
            body = bytearray(raw) + bytes(max(0, s["total"] - len(raw)))
        mem[i] = bytearray(body[:max(s["total"], len(body))])
    li = next(i for i, s in enumerate(secs) if s["kind"] == 4)
    L = d[secs[li]["off"]:secs[li]["off"] + secs[li]["packed"]]
    (mainS, mainO, initS, initO, termS, termO, nlibs, nsyms, nrelsec, relinstoff, strtab, _, _, _) = \
        struct.unpack_from(">iIiIiIIIIIIIII", L, 0)

    def s(o):
        return L[strtab + o:L.index(b"\0", strtab + o)].decode("latin-1")

    imports = []
    symbase = 56 + 24 * nlibs
    for k in range(nlibs):
        nm, _, _, nimp, first, _, _, _ = struct.unpack_from(">IIIIIBBH", L, 56 + 24 * k)
        for j in range(first, first + nimp):
            w = struct.unpack_from(">I", L, symbase + 4 * j)[0]
            imports.append((s(nm), s(w & 0xFFFFFF), (w >> 24) & 0xF))
    impaddr = [resolve(lib, sym, cls) for lib, sym, cls in imports]

    relhdr = symbase + 4 * nsyms
    for r in range(nrelsec):
        sec, _, count, first = struct.unpack_from(">HHII", L, relhdr + 12 * r)
        buf = mem[sec]
        instrs = [struct.unpack_from(">H", L, relinstoff + first + 2 * k)[0] for k in range(count)]
        st = {"pos": 0, "imp": 0, "C": bases.get(0, 0), "D": bases.get(1, 0)}

        def add(v):
            o = st["pos"]
            w = struct.unpack_from(">I", buf, o)[0]
            struct.pack_into(">I", buf, o, (w + v) & 0xFFFFFFFF)
            st["pos"] += 4

        def put(v):
            struct.pack_into(">I", buf, st["pos"], v & 0xFFFFFFFF)
            st["pos"] += 4

        def run(k):
            """execute instruction at index k, return index of next"""
            ins = instrs[k]
            if ins >> 14 == 0:                        # RelocBySectDWithSkip
                st["pos"] += 4 * ((ins >> 6) & 0xFF)
                for _ in range(ins & 0x3F):
                    add(st["D"])
                return k + 1
            if ins >> 13 == 2:                        # RelocRun group
                sub, n = (ins >> 9) & 0xF, (ins & 0x1FF) + 1
                for _ in range(n):
                    if sub == 0:
                        add(st["C"])
                    elif sub == 1:
                        add(st["D"])
                    elif sub == 2:
                        add(st["C"]); add(st["D"]); st["pos"] += 4
                    elif sub == 3:
                        add(st["C"]); add(st["D"])
                    elif sub == 4:
                        add(st["D"]); st["pos"] += 4
                    elif sub == 5:
                        put(impaddr[st["imp"]]); st["imp"] += 1
                    else:
                        raise ValueError(f"reloc run sub {sub}")
                return k + 1
            if ins >> 13 == 3:                        # RelocSmall group
                sub, idx = (ins >> 9) & 0xF, ins & 0x1FF
                if sub == 0:
                    put(impaddr[idx]); st["imp"] = idx + 1
                elif sub == 1:
                    st["C"] = bases[idx]
                elif sub == 2:
                    st["D"] = bases[idx]
                elif sub == 3:
                    add(bases[idx])
                else:
                    raise ValueError(f"reloc small sub {sub}")
                return k + 1
            if ins >> 12 == 8:                        # RelocIncrPosition
                st["pos"] += (ins & 0xFFF) + 1
                return k + 1
            if ins >> 12 == 9:                        # RelocSmRepeat
                blk, rep = ((ins >> 8) & 0xF) + 1, (ins & 0xFF) + 1
                for _ in range(rep):
                    j = k - blk
                    while j < k:
                        j = run(j)
                return k + 1
            top6 = ins >> 10
            if top6 in (0x28, 0x29, 0x2C, 0x2D):
                v = ((ins & 0x3FF) << 16) | instrs[k + 1]
                if top6 == 0x28:                      # RelocSetPosition
                    st["pos"] = v
                elif top6 == 0x29:                    # RelocLgByImport
                    put(impaddr[v]); st["imp"] = v + 1
                elif top6 == 0x2C:                    # RelocLgRepeat
                    blk, rep = ((ins >> 6) & 0xF) + 1, ((ins & 0x3F) << 16) | instrs[k + 1]
                    for _ in range(rep):
                        j = k - blk
                        while j < k:
                            j = run(j)
                else:                                 # RelocLgSetOrBySection
                    sub, idx = (ins >> 6) & 0xF, ((ins & 0x3F) << 16) | instrs[k + 1]
                    if sub == 0:
                        add(bases[idx])
                    elif sub == 1:
                        st["C"] = bases[idx]
                    elif sub == 2:
                        st["D"] = bases[idx]
                return k + 2
            raise ValueError(f"unknown reloc opcode {ins:#06x}")

        k = 0
        while k < len(instrs):
            k = run(k)
    main = bases[mainS] + mainO if mainS >= 0 else None
    return {"mem": mem, "imports": imports, "impaddr": impaddr, "main": main, "secs": secs}
