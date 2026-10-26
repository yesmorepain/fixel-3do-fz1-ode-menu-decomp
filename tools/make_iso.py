#!/usr/bin/env python3
"""Write rebuilt programs into a copy of boot.iso.

Usage: make_iso.py <original.iso> <out.iso> <disc path>=<file> ...
Each file replaces the disc file in place (every avatar). The size must stay the same: directories, block layout
and the disc's `signatures` file are not rewritten, so a file whose bytes differ from the original is reported
(a signed disc with changed files may be rejected). Prints the sha256 of the result.
"""
import hashlib
import sys

from opera import Opera


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    data = bytearray(open(sys.argv[1], "rb").read())
    fs = Opera(bytes(data))
    entries = dict(fs.walk())
    for arg in sys.argv[3:]:
        path, src = arg.split("=", 1)
        e = entries.get("/" + path.lstrip("/"))
        if e is None:
            sys.exit(f"{path}: not on the disc")
        new = open(src, "rb").read()
        if len(new) != e["size"]:
            sys.exit(f"{path}: size {len(new)} != {e['size']} on the disc")
        same = new == fs.file_data(e)
        for a in e["avatars"]:
            o = a * e["block_size"]
            data[o:o + len(new)] = new
        print(f"{path}: {len(new)} bytes, {'identical to' if same else 'DIFFERS from'} the original")
    open(sys.argv[2], "wb").write(data)
    print(f"{sys.argv[2]}: sha256 {hashlib.sha256(data).hexdigest()}")


if __name__ == "__main__":
    main()
