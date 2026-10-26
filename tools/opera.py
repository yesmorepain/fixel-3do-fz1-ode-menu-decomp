#!/usr/bin/env python3
"""Minimal 3DO Opera filesystem lister/extractor (pure Python 3, no dependencies).

Usage:
  python3 opera.py list    <image.iso>
  python3 opera.py extract <image.iso> <outdir>

Layout (all integers big-endian):
  Volume label @ block 0: record type 1, sync 5x0x5A, version, flags, comment[32], label[32],
    id, block_size, block_count, root_dir_id, root_dir_blocks, root_dir_block_size,
    last_root_dir_copy, root_dir_copies[8].
  Directory block: next_block, prev_block, flags, first_free_byte, first_entry_offset; then entries:
    flags, id, type[4], block_size, byte_count, block_count, burst, gap, name[32],
    last_avatar_index, avatars[last_avatar_index + 1].
  Entry flags: low byte 2 = file, 7 = directory; 0x40000000 = last in block, 0x80000000 = last in dir.
"""
import os
import struct
import sys

LAST_IN_BLOCK = 0x40000000
LAST_IN_DIR = 0x80000000


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


class Opera:
    def __init__(self, data):
        self.d = data
        if data[0] != 1 or data[1:6] != b"\x5a" * 5:
            raise ValueError("not an Opera volume")
        self.label = data[0x28:0x48].split(b"\0")[0].decode("latin-1")
        self.comment = data[0x08:0x28].split(b"\0")[0].decode("latin-1")
        self.vol_id = u32(data, 0x48)
        self.bs = u32(data, 0x4C)
        self.blocks = u32(data, 0x50)
        self.root_id = u32(data, 0x54)
        self.root_blocks = u32(data, 0x58)
        self.root_bs = u32(data, 0x5C)
        last = u32(data, 0x60)
        self.root_copies = [u32(data, 0x64 + 4 * i) for i in range(last + 1)]

    def block(self, n, bs=None):
        bs = bs or self.bs
        return self.d[n * bs:(n + 1) * bs]

    def read_dir(self, start, nblocks, bs):
        """Yield entries of the directory stored in nblocks blocks from 'start'."""
        blk = 0
        while blk != 0xFFFFFFFF and blk < nblocks:
            b = self.block(start + blk, bs)
            nxt = u32(b, 0)
            off = u32(b, 16)
            while True:
                flags = u32(b, off)
                last = u32(b, off + 64)
                e = {
                    "flags": flags,
                    "id": u32(b, off + 4),
                    "type": b[off + 8:off + 12].decode("latin-1"),
                    "block_size": u32(b, off + 12),
                    "size": u32(b, off + 16),
                    "nblocks": u32(b, off + 20),
                    "burst": u32(b, off + 24),
                    "gap": u32(b, off + 28),
                    "name": b[off + 32:off + 64].split(b"\0")[0].decode("latin-1"),
                    "avatars": [u32(b, off + 68 + 4 * i) for i in range(last + 1)],
                }
                yield e
                off += 68 + 4 * (last + 1)
                if flags & (LAST_IN_BLOCK | LAST_IN_DIR):
                    break
            if flags & LAST_IN_DIR:
                break
            blk = nxt

    def walk(self, start=None, nblocks=None, bs=None, path=""):
        if start is None:
            start, nblocks, bs = self.root_copies[0], self.root_blocks, self.root_bs
        for e in self.read_dir(start, nblocks, bs):
            p = path + "/" + e["name"]
            yield p, e
            if (e["flags"] & 0xFF) == 7:
                yield from self.walk(e["avatars"][0], e["nblocks"], e["block_size"], p)

    def file_data(self, e):
        o = e["avatars"][0] * e["block_size"]
        return self.d[o:o + e["size"]]


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("list", "extract"):
        sys.exit(__doc__)
    with open(sys.argv[2], "rb") as f:
        fs = Opera(f.read())
    if sys.argv[1] == "list":
        print(f"label={fs.label!r} comment={fs.comment!r} id={fs.vol_id:#x} bs={fs.bs} blocks={fs.blocks} "
              f"root@{[hex(x) for x in fs.root_copies]}")
        print(f"{'flags':>8} {'type':4} {'size':>9} {'block':>6} {'bs':>5}  path")
    for p, e in fs.walk():
        isdir = (e["flags"] & 0xFF) == 7
        if sys.argv[1] == "list":
            print(f"{e['flags']:08x} {e['type']:4} {e['size']:9} {e['avatars'][0]:6x} {e['block_size']:5}  {p}"
                  + ("/" if isdir else ""))
        else:
            out = os.path.join(sys.argv[3], p.lstrip("/"))
            if isdir:
                os.makedirs(out, exist_ok=True)
            else:
                os.makedirs(os.path.dirname(out), exist_ok=True)
                with open(out, "wb") as f:
                    f.write(fs.file_data(e))


if __name__ == "__main__":
    main()
