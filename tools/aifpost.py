#!/usr/bin/env python3
"""Post-link steps of the SDK 2.5 debug build, reimplemented from their observable effect on the AIF:

    modbin <aif> -stack N [-debug]     (3DO header present flag, stack size, debug marker)
    stripaif <aif> -o <out>            (drop the debug area and its relocations)

Usage: python3 aifpost.py <in.aif> <out> [--stack N] [--debug] [--flags N] [--name S] [--sig-from ORIG]

modbin (field offsets as documented by trapexit/modbin, tdo_aif.c):
  byte 0x2c = 0x40 (3DO header present), word 0xa8 = stack size, word 0x40 = 0xEF00010A with -debug,
  byte 0xa4 = flags (0x02 privileged: ode driver), 0xc0 = name (32 bytes, "odedrv").
  The signature fields 0xb0/0xb4 and the signature appended to the file come from the signing step, which cannot
  be redone (private key). --sig-from ORIG copies them from the original file instead (borrowed, not regenerated);
  it refuses unless every byte before the signature, except 0xb0/0xb4, already equals ORIG.
stripaif:
  removes the debug area (between RW and the self-relocation code), removes relocation entries that point into
  it, sets debug size (0x1c) and debug type (0x24) to 0 and re-targets the BL at 0x04 to the self-reloc code.
The original tools are not available; this reproduces the original LaunchMe and ode driver byte for byte (docs/objects.md).
"""
import struct
import sys

SELFRELOC_CODE = 0xb8          # bytes of self-relocation code before the relocation list


def main():
    args = sys.argv[1:]
    stack, debug, flags, name, sigfrom = None, False, None, None, None
    if "--sig-from" in args:
        i = args.index("--sig-from"); sigfrom = args[i + 1]; del args[i:i + 2]
    if "--flags" in args:
        i = args.index("--flags"); flags = int(args[i + 1], 0); del args[i:i + 2]
    if "--name" in args:
        i = args.index("--name"); name = args[i + 1].encode(); del args[i:i + 2]
    if "--stack" in args:
        i = args.index("--stack"); stack = int(args[i + 1], 0); del args[i:i + 2]
    if "--debug" in args:
        args.remove("--debug"); debug = True
    src, dst = args
    d = bytearray(open(src, "rb").read())
    ro, rw, dbg = struct.unpack_from(">III", d, 0x14)
    img = ro + rw
    # modbin
    d[0x2c] = 0x40
    if stack is not None:
        struct.pack_into(">I", d, 0xa8, stack)
    if debug:
        struct.pack_into(">I", d, 0x40, 0xEF00010A)
    if flags is not None:
        d[0xa4] = flags
    if name is not None:
        d[0xc0:0xe0] = name.ljust(32, b"\0")[:32]
    # stripaif
    tail = d[img + dbg:]
    code, rel = tail[:SELFRELOC_CODE], tail[SELFRELOC_CODE:]
    words = [struct.unpack_from(">I", rel, o)[0] for o in range(0, len(rel), 4)]
    assert words[-1] == 0xFFFFFFFF, "relocation list not terminated"
    keep = [w for w in words[:-1] if w < img] + [0xFFFFFFFF]
    out = d[:img] + code + b"".join(struct.pack(">I", w) for w in keep)
    struct.pack_into(">I", out, 0x1c, 0)
    struct.pack_into(">I", out, 0x24, 0)
    struct.pack_into(">I", out, 0x04, 0xEB000000 | (((img - 0x04 - 8) >> 2) & 0xFFFFFF))
    if sigfrom:
        orig = open(sigfrom, "rb").read()
        sigoff, siglen = struct.unpack_from(">II", orig, 0xb0)
        assert sigoff == len(out) and sigoff + siglen == len(orig), "signature layout does not fit this build"
        if out[:0xb0] + out[0xb8:] != orig[:0xb0] + orig[0xb8:sigoff]:
            sys.exit("--sig-from: build differs from the original before the signature; not copying it")
        out[0xb0:0xb8] = orig[0xb0:0xb8]
        out += orig[sigoff:]
    open(dst, "wb").write(out)


if __name__ == "__main__":
    main()
