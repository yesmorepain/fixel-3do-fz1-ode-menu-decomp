#!/usr/bin/env python3
"""Run a PowerPC MPW tool (PEF, e.g. the 3DO SDK's ARM C 4.62 'armcc') on a modern host.

Usage: mpwrun.py [-v] <tool> [args...]

The tool's code runs in Unicorn (PPC32 big-endian). Every imported StdCLib / InterfaceLib function is a trap that
is implemented here in Python (stdio, strings, memory, a few Mac Toolbox no-ops). Mac paths (':' separators) are
translated to host paths. Exit status = the tool's exit status.
Requires: unicorn (see requirements.txt; the build scripts use .venv/bin/python).
"""
import os
import struct
import sys
import time as _time

from unicorn import Uc, UC_ARCH_PPC, UC_MODE_PPC32, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
from unicorn.ppc_const import (UC_PPC_REG_PC, UC_PPC_REG_LR, UC_PPC_REG_1, UC_PPC_REG_2, UC_PPC_REG_3,
                               UC_PPC_REG_4, UC_PPC_REG_5, UC_PPC_REG_6, UC_PPC_REG_7, UC_PPC_REG_8,
                               UC_PPC_REG_9, UC_PPC_REG_10)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pef import load_pef  # noqa: E402

CODE, DATA, TVEC, STUB, IMPDATA, HEAP, STACK = (0x10000000, 0x20000000, 0x30000000, 0x30100000,
                                                 0x31000000, 0x40000000, 0x7F000000)
HEAPSIZE, STACKSIZE, EXIT_ADDR = 0x08000000, 0x00400000, 0x3FFF0000
GPR = [UC_PPC_REG_3, UC_PPC_REG_4, UC_PPC_REG_5, UC_PPC_REG_6, UC_PPC_REG_7, UC_PPC_REG_8, UC_PPC_REG_9,
       UC_PPC_REG_10]
FILESZ = 0x18                              # sizeof(FILE) in MPW PPC StdCLib (stdout = _iob + 0x18)
EOF_ = 0xFFFFFFFF

# MPW CType.h bits (from the masks armcc tests, e.g. isprint = 0x57): 01 upper, 02 lower, 04 digit,
# 08 white space, 10 punct, 20 control, 40 blank (space), 80 hex digit
CTYPE = bytearray(256)
for c in range(256):
    ch = chr(c)
    v = 0
    if "A" <= ch <= "Z": v |= 0x01
    if "a" <= ch <= "z": v |= 0x02
    if "0" <= ch <= "9": v |= 0x04
    if ch in " \t\n\r\v\f": v |= 0x08
    if 33 <= c <= 126 and not ch.isalnum(): v |= 0x10
    if c < 32 or c == 127: v |= 0x20
    if ch == " ": v |= 0x40
    if ch in "0123456789abcdefABCDEF": v |= 0x80
    CTYPE[c] = v


def hostpath(p):
    if ":" not in p:
        return p
    if p.startswith("/"):                  # host absolute dir + ':' + name
        return p.replace(":", "/")
    rel = p.startswith(":")
    p = p.lstrip(":") if rel else p
    p = p.replace("::", "/../").replace(":", "/")
    return p if rel else "/" + p


class Exit(Exception):
    pass


class TextFile:
    """MPW text stream: '\n' is CR (0x0D) on the Mac side, LF on the host."""
    def __init__(self, f):
        self.f = f

    def read(self, n=-1):
        return self.f.read(n).replace(b"\n", b"\r")

    def write(self, b):
        return self.f.write(bytes(b).replace(b"\r", b"\n"))

    def seek(self, *a):
        return self.f.seek(*a)

    def tell(self):
        return self.f.tell()

    def flush(self):
        return self.f.flush()

    def close(self):
        return self.f.close()


class Runner:
    def __init__(self, tool, args, verbose=False):
        self.v = verbose
        self.uc = uc = Uc(UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN)
        d = open(tool, "rb").read()
        self.stubs, self.datimp = [], {}
        self.impdata_next = IMPDATA
        p = load_pef(d, {0: CODE, 1: DATA}, self.resolve)
        for base, sec in ((CODE, 0), (DATA, 1)):
            body = bytes(p["mem"][sec])
            size = (len(body) + 0xFFFF) & ~0xFFFF
            uc.mem_map(base, size)
            uc.mem_write(base, body)
        self.stubs.append("__callback_ret")       # return address for calls from Python into emulated code
        n = len(self.stubs)
        self.cbret = STUB + 4 * (n - 1)
        self.cb = None
        uc.mem_map(TVEC, 0x100000)
        uc.mem_map(STUB, 0x100000)
        for i in range(n):
            uc.mem_write(TVEC + 8 * i, struct.pack(">II", STUB + 4 * i, 0))
            uc.mem_write(STUB + 4 * i, struct.pack(">I", 0x4E800020))      # blr
        uc.mem_map(IMPDATA, 0x100000)
        uc.mem_map(HEAP, HEAPSIZE)
        uc.mem_map(STACK, STACKSIZE)
        uc.mem_map(EXIT_ADDR & ~0xFFFF, 0x10000)
        uc.hook_add(UC_HOOK_CODE, self.trap, begin=STUB, end=STUB + 4 * n)
        self.heap = HEAP + 0x1000
        self.sizes = {}
        # data imports
        dv = self.datimp
        self.iob = dv.get("_iob")
        self.files = {}
        if self.iob:
            for k, f in enumerate((STDIN, STDOUT, STDERR)):
                self.files[self.iob + FILESZ * k] = f
        if "__p_CType" in dv:
            tbl = self.malloc(256)
            uc.mem_write(tbl, bytes(CTYPE))
            self.w32(dv["__p_CType"], tbl)
        self.fds = {0: STDIN, 1: STDOUT, 2: STDERR}
        self.status = 0
        self.main = p["main"]
        # argv / envp in _IntEnv: +2 argc, +6 argv, +0xa envp
        argv = [self.cstr_alloc(a) for a in [os.path.basename(tool)] + args]
        av = self.malloc(4 * (len(argv) + 1))
        for k, a in enumerate(argv):
            self.w32(av + 4 * k, a)
        self.w32(av + 4 * len(argv), 0)
        ev = self.malloc(4)
        self.w32(ev, 0)
        if "_IntEnv" in dv:
            ie = dv["_IntEnv"]
            uc.mem_write(ie + 2, struct.pack(">III", len(argv), av, ev))
        self.atexit = []

    # ---- imports
    def resolve(self, lib, sym, cls):
        if cls == 2 or (cls == 0):                # TVector (code)
            self.stubs.append(sym)
            return TVEC + 8 * (len(self.stubs) - 1)
        a = self.impdata_next                      # data
        self.impdata_next += 0x400
        self.datimp[sym] = a
        return a

    # ---- memory helpers
    def r32(self, a):
        return struct.unpack(">I", self.uc.mem_read(a, 4))[0]

    def w32(self, a, v):
        self.uc.mem_write(a, struct.pack(">I", v & 0xFFFFFFFF))

    def cstr(self, a):
        out = bytearray()
        while True:
            chunk = self.uc.mem_read(a, 256)
            z = chunk.find(b"\0")
            if z >= 0:
                out += chunk[:z]
                return bytes(out)
            out += chunk
            a += 256

    def cstr_alloc(self, s):
        b = s.encode("latin-1") + b"\0"
        a = self.malloc(len(b))
        self.uc.mem_write(a, b)
        return a

    def malloc(self, n):
        n = max(n, 1)
        a = (self.heap + 7) & ~7
        self.heap = a + n
        if self.heap > HEAP + HEAPSIZE:
            raise MemoryError("emulated heap exhausted")
        self.sizes[a] = n
        return a

    def reg(self, i):
        return self.uc.reg_read(GPR[i])

    def args(self):
        """generator over argument words: r3..r10, then the caller's parameter area (r1+56...)"""
        for i in range(8):
            yield self.reg(i)
        sp = self.uc.reg_read(UC_PPC_REG_1) + 56
        while True:
            yield self.r32(sp)
            sp += 4

    # ---- printf
    def fmt(self, f, words):
        out = bytearray()
        i = 0
        while i < len(f):
            c = f[i]
            if c != 0x25:
                out.append(c); i += 1; continue
            j = i + 1
            if j < len(f) and f[j] == 0x25:
                out.append(0x25); i = j + 1; continue
            spec = "%"
            while j < len(f) and chr(f[j]) in "-+ #0":
                spec += chr(f[j]); j += 1
            while j < len(f) and (chr(f[j]).isdigit() or f[j] == 0x2A):
                spec += str(ctypes_int(next(words))) if f[j] == 0x2A else chr(f[j]); j += 1
            if j < len(f) and f[j] == 0x2E:
                spec += "."; j += 1
                while j < len(f) and (chr(f[j]).isdigit() or f[j] == 0x2A):
                    spec += str(ctypes_int(next(words))) if f[j] == 0x2A else chr(f[j]); j += 1
            while j < len(f) and chr(f[j]) in "hlLq":
                j += 1
            conv = chr(f[j]); j += 1
            if conv in "di":
                out += (spec + "d").encode() % ctypes_int(next(words))
            elif conv in "uxXo":
                out += (spec + conv).encode() % next(words)
            elif conv == "c":
                out += (spec + "c").encode() % (next(words) & 0xFF)
            elif conv == "s":
                out += (spec + "s").encode() % self.cstr(next(words))
            elif conv == "p":
                out += b"%08x" % next(words)
            elif conv in "feEgG":
                hi, lo = next(words), next(words)
                out += (spec + conv).encode() % struct.unpack(">d", struct.pack(">II", hi, lo))[0]
            elif conv == "n":
                self.w32(next(words), len(out))
            else:
                out += (spec + conv).encode()
            i = j
        return bytes(out)

    def va_words(self, ap):
        while True:
            yield self.r32(ap)
            ap += 4

    # ---- file helpers
    def fobj(self, fp):
        return self.files.get(fp)

    def newfile(self, fobj):
        fp = self.malloc(FILESZ)
        self.uc.mem_write(fp, bytes(FILESZ))
        self.files[fp] = fobj
        return fp

    # ---- trap dispatch
    def trap(self, uc, addr, size, _):
        name = self.stubs[(addr - STUB) // 4]
        fn = getattr(self, "f_" + name, None)
        if fn is None:
            sys.stderr.write(f"mpwrun: unimplemented import {name}\n")
            raise Exit(127)
        try:
            r = fn()
        except Exit as e:
            self.status = e.args[0]
            uc.emu_stop()
            return
        if self.v:
            sys.stderr.write(f"[{name} -> {r}]\n")
        if r is not None:
            uc.reg_write(UC_PPC_REG_3, r & 0xFFFFFFFF)

    def run(self):
        uc = self.uc
        pc, toc = self.r32(self.main), self.r32(self.main + 4)
        uc.reg_write(UC_PPC_REG_1, STACK + STACKSIZE - 256)
        uc.reg_write(UC_PPC_REG_2, toc)
        uc.reg_write(UC_PPC_REG_LR, EXIT_ADDR)
        try:
            uc.emu_start(pc, EXIT_ADDR)
        except Exit as e:
            self.status = e.args[0]
        else:
            if uc.reg_read(UC_PPC_REG_PC) == EXIT_ADDR:
                self.status = uc.reg_read(UC_PPC_REG_3)
        for f in list(self.files.values()) + list(self.fds.values()):
            try:
                f.flush()
            except Exception:
                pass
        return ctypes_int(self.status)

    # ================= StdCLib =================
    def f___setjmp(self): return 0
    def f__BreakPoint(self): return 0

    def f_exit(self):
        self.status = self.reg(0)
        raise Exit(self.status)

    def f_abort(self): raise Exit(134)
    def f_atexit(self): return 0
    def f_signal(self): return 0
    def f_system(self): return 0xFFFFFFFF
    def f_getenv(self): return 0
    def f_time(self):
        t = int(_time.time()) + 2082844800      # Mac epoch 1904
        if self.reg(0):
            self.w32(self.reg(0), t)
        return t

    def f_clock(self): return int(_time.process_time() * 60)

    def f_ctime(self):
        t = self.r32(self.reg(0)) - 2082844800
        return self.cstr_alloc(_time.ctime(t) + "\n")

    def f_malloc(self): return self.malloc(self.reg(0))
    def f_free(self): return 0

    def f_realloc(self):
        old, n = self.reg(0), self.reg(1)
        a = self.malloc(n)
        if old:
            m = min(self.sizes.get(old, n), n)
            self.uc.mem_write(a, bytes(self.uc.mem_read(old, m)))
        return a

    def f_memcpy(self):
        d, s, n = self.reg(0), self.reg(1), self.reg(2)
        if n:
            self.uc.mem_write(d, bytes(self.uc.mem_read(s, n)))
        return d

    def f_memset(self):
        d, c, n = self.reg(0), self.reg(1), self.reg(2)
        if n:
            self.uc.mem_write(d, bytes([c & 0xFF]) * n)
        return d

    def f_memcmp(self):
        a, b, n = self.reg(0), self.reg(1), self.reg(2)
        x, y = bytes(self.uc.mem_read(a, n)), bytes(self.uc.mem_read(b, n))
        return (x > y) - (x < y)

    def f_strlen(self): return len(self.cstr(self.reg(0)))

    def f_strcpy(self):
        d = self.reg(0)
        self.uc.mem_write(d, self.cstr(self.reg(1)) + b"\0")
        return d

    def f_strncpy(self):
        d, s, n = self.reg(0), self.cstr(self.reg(1)), self.reg(2)
        self.uc.mem_write(d, (s[:n] + bytes(max(0, n - len(s))))[:n])
        return d

    def f_strcat(self):
        d = self.reg(0)
        self.uc.mem_write(d + len(self.cstr(d)), self.cstr(self.reg(1)) + b"\0")
        return d

    def f_strcmp(self):
        a, b = self.cstr(self.reg(0)), self.cstr(self.reg(1))
        return (a > b) - (a < b)

    def f_strncmp(self):
        n = self.reg(2)
        a, b = self.cstr(self.reg(0))[:n], self.cstr(self.reg(1))[:n]
        return (a > b) - (a < b)

    def f_strchr(self):
        s, c = self.reg(0), self.reg(1) & 0xFF
        b = self.cstr(s)
        if c == 0:
            return s + len(b)
        k = b.find(bytes([c]))
        return s + k if k >= 0 else 0

    def _strtol(self, signed):
        s, endp, base = self.reg(0), self.reg(1), ctypes_int(self.reg(2))
        b = self.cstr(s)
        i = 0
        while i < len(b) and b[i] in b" \t\n\r\f\v":
            i += 1
        neg = False
        if i < len(b) and b[i] in b"+-":
            neg = b[i] == 0x2D; i += 1
        if base in (0, 16) and b[i:i + 2].lower() == b"0x":
            i += 2; base = 16
        elif base == 0 and b[i:i + 1] == b"0":
            base = 8
        elif base == 0:
            base = 10
        v, start = 0, i
        while i < len(b):
            ch = chr(b[i]).lower()
            dgt = int(ch, 36) if ch.isalnum() else 99
            if dgt >= base:
                break
            v = v * base + dgt; i += 1
        if endp:
            self.w32(endp, s + (i if i > start else 0))
        return -v if neg else v

    def f_strtol(self): return self._strtol(True)
    def f_strtoul(self): return self._strtol(False)
    def f_toupper(self):
        c = self.reg(0)
        return c - 32 if 97 <= c <= 122 else c

    def f_tolower(self):
        c = self.reg(0)
        return c + 32 if 65 <= c <= 90 else c

    def f_qsort(self):
        # The comparisons run in emulated code: a generator yields (a, b) pairs, f___callback_ret resumes it.
        base, nel, width, cmp = self.reg(0), self.reg(1), self.reg(2), self.reg(3)
        uc = self.uc

        def sort():
            elems = [bytes(uc.mem_read(base + i * width, width)) for i in range(nel)]
            tmp = self.malloc(2 * width)

            def less_eq(x, y):           # yields the call, receives cmp's result
                uc.mem_write(tmp, elems[x]); uc.mem_write(tmp + width, elems[y])
                r = yield (tmp, tmp + width)
                return ctypes_int(r) <= 0

            idx = list(range(nel))       # merge sort (stable; MPW's order of equal keys is not known)
            w = 1
            while w < nel:
                out = []
                for lo in range(0, nel, 2 * w):
                    a, b = idx[lo:lo + w], idx[lo + w:lo + 2 * w]
                    i = j = 0
                    while i < len(a) and j < len(b):
                        if (yield from less_eq(a[i], b[j])):
                            out.append(a[i]); i += 1
                        else:
                            out.append(b[j]); j += 1
                    out += a[i:] + b[j:]
                idx, w = out, 2 * w
            uc.mem_write(base, b"".join(elems[i] for i in idx))

        self.cb = (sort(), self.r32(cmp), self.r32(cmp + 4), uc.reg_read(UC_PPC_REG_LR), uc.reg_read(UC_PPC_REG_2))
        self._cb_step(None)
        return None

    def _cb_step(self, value):
        uc = self.uc
        gen, pc, toc, lr, r2 = self.cb
        try:
            a, b = gen.send(value)
        except StopIteration:            # done: let this stub's blr return to the original caller
            self.cb = None
            uc.reg_write(UC_PPC_REG_LR, lr)
            uc.reg_write(UC_PPC_REG_2, r2)
            return
        uc.reg_write(GPR[0], a)
        uc.reg_write(GPR[1], b)
        uc.reg_write(UC_PPC_REG_2, toc)
        uc.reg_write(UC_PPC_REG_LR, self.cbret)
        uc.reg_write(UC_PPC_REG_PC, pc)

    def f___callback_ret(self):
        self._cb_step(self.reg(0))
        return None

    # stdio
    def f_fopen(self):
        path, mode = hostpath(self.cstr(self.reg(0)).decode("latin-1")), self.cstr(self.reg(1)).decode()
        m = mode.replace("b", "").replace("t", "") + "b"
        try:
            f = open(path, m)
            if "b" not in mode:
                f = TextFile(f)
        except OSError:
            if self.v:
                sys.stderr.write(f"[fopen {path} {mode} failed]\n")
            return 0
        if self.v:
            sys.stderr.write(f"[fopen {path} {mode}]\n")
        return self.newfile(f)

    def f_freopen(self):
        path, mode, fp = (hostpath(self.cstr(self.reg(0)).decode("latin-1")), self.cstr(self.reg(1)).decode(),
                          self.reg(2))
        try:
            f = open(path, mode.replace("b", "").replace("t", "") + "b")
            self.files[fp] = f if "b" in mode else TextFile(f)
            self.uc.mem_write(fp, bytes(FILESZ))
        except OSError:
            return 0
        return fp

    def f_fclose(self):
        f = self.files.pop(self.reg(0), None)
        if f and f not in (STDIN, STDOUT, STDERR):
            f.close()
        return 0

    def f_setvbuf(self): return 0

    def f_fread(self):
        buf, size, n, fp = self.reg(0), self.reg(1), self.reg(2), self.reg(3)
        data = self.fobj(fp).read(size * n)
        self.uc.mem_write(buf, data)
        if len(data) < size * n:
            self.seteof(fp)
        return len(data) // size if size else 0

    def f_fwrite(self):
        buf, size, n, fp = self.reg(0), self.reg(1), self.reg(2), self.reg(3)
        self.fobj(fp).write(bytes(self.uc.mem_read(buf, size * n)))
        return n

    def f_fseek(self):
        fp, off, wh = self.reg(0), ctypes_int(self.reg(1)), self.reg(2)
        try:
            self.fobj(fp).seek(off, wh)
            return 0
        except OSError:
            return EOF_

    def f_ftell(self): return self.fobj(self.reg(0)).tell()

    def f_fputs(self):
        self.fobj(self.reg(1)).write(self.cstr(self.reg(0)))
        return 0

    def f_fputc(self):
        self.fobj(self.reg(1)).write(bytes([self.reg(0) & 0xFF]))
        return self.reg(0) & 0xFF

    def f_putchar(self):
        STDOUT.write(bytes([self.reg(0) & 0xFF]))
        return self.reg(0) & 0xFF

    def f__flsbuf(self):
        c, fp = self.reg(0), self.reg(1)
        self.fobj(fp).write(bytes([c & 0xFF]))
        self.w32(fp, 0)
        return c & 0xFF

    def seteof(self, fp):
        """FILE._flag (u16 @+0x12) bit 0x10 = _IOEOF (tested by armcc's line reader)"""
        fl = struct.unpack(">H", self.uc.mem_read(fp + 0x12, 2))[0]
        self.uc.mem_write(fp + 0x12, struct.pack(">H", fl | 0x10))

    def f__filbuf(self):
        fp = self.reg(0)
        self.w32(fp, 0)
        b = self.fobj(fp).read(1)
        if not b:
            self.seteof(fp)
            return EOF_
        return b[0]

    def f_printf(self):
        w = self.args(); f = self.cstr(next(w))
        out = self.fmt(f, w); STDOUT.write(out); return len(out)

    def f_fprintf(self):
        w = self.args(); fp = next(w); f = self.cstr(next(w))
        out = self.fmt(f, w); self.fobj(fp).write(out); return len(out)

    def f_vfprintf(self):
        fp, f, ap = self.reg(0), self.cstr(self.reg(1)), self.reg(2)
        out = self.fmt(f, self.va_words(ap)); self.fobj(fp).write(out); return len(out)

    def f_sprintf(self):
        w = self.args(); d = next(w); f = self.cstr(next(w))
        out = self.fmt(f, w); self.uc.mem_write(d, out + b"\0"); return len(out)

    def f_vsprintf(self):
        d, f, ap = self.reg(0), self.cstr(self.reg(1)), self.reg(2)
        out = self.fmt(f, self.va_words(ap)); self.uc.mem_write(d, out + b"\0"); return len(out)

    def f_remove(self):
        try:
            os.remove(hostpath(self.cstr(self.reg(0)).decode("latin-1")))
            return 0
        except OSError:
            return EOF_

    # low-level I/O (armlink)
    def f_open(self):
        path, flags = hostpath(self.cstr(self.reg(0)).decode("latin-1")), self.reg(1)
        # MPW fcntl: O_RDONLY 0, O_WRONLY 1, O_RDWR 2, O_APPEND 8, O_CREAT 0x100, O_TRUNC 0x200
        acc = flags & 3
        try:
            if acc == 0:
                f = open(path, "rb")
            else:
                if flags & 0x200 or not os.path.exists(path):
                    open(path, "wb").close()
                f = open(path, "r+b")
                if flags & 8:
                    f.seek(0, 2)
        except OSError:
            if self.v:
                sys.stderr.write(f"[open {path} {flags:#x} failed]\n")
            return EOF_
        fd = max(self.fds) + 1
        self.fds[fd] = f
        if self.v:
            sys.stderr.write(f"[open {path} {flags:#x} -> {fd}]\n")
        return fd

    def f_close(self):
        f = self.fds.pop(self.reg(0), None)
        if f and self.reg(0) > 2:
            f.close()
        return 0

    def f_read(self):
        fd, buf, n = self.reg(0), self.reg(1), self.reg(2)
        data = self.fds[fd].read(n)
        self.uc.mem_write(buf, data)
        return len(data)

    def f_write(self):
        fd, buf, n = self.reg(0), self.reg(1), self.reg(2)
        self.fds[fd].write(bytes(self.uc.mem_read(buf, n)))
        return n

    def f_lseek(self):
        fd, off, wh = self.reg(0), ctypes_int(self.reg(1)), self.reg(2)
        return self.fds[fd].seek(off, wh)

    # ================= InterfaceLib (minimal) =================
    def f_HGetState(self): return 0
    def f_HSetState(self): return 0
    def f_SetCursor(self): return 0
    def f_HideCursor(self): return 0
    def f_ShowCursor(self): return 0
    def f_CurResFile(self): return 0
    def f_HomeResFile(self): return 0
    def f_SetResLoad(self): return 0
    def f_GetResource(self): return 0
    def f_ReleaseResource(self): return 0
    def f_GetCursor(self): return 0
    def f_LMGetCurrentA5(self): return 0
    def f_MemError(self): return 0
    def f_HLock(self): return 0
    def f_DisposeHandle(self): return 0
    def f_TempDisposeHandle(self): return 0

    def _newhandle(self, n):
        p = self.malloc(n)
        h = self.malloc(4)
        self.w32(h, p)
        return h

    def f_NewHandle(self): return self._newhandle(self.reg(0))

    def f_TempNewHandle(self):
        if self.reg(1):
            self.uc.mem_write(self.reg(1), b"\0\0")
        return self._newhandle(self.reg(0))


STDIN, STDOUT, STDERR = TextFile(sys.stdin.buffer), TextFile(sys.stdout.buffer), TextFile(sys.stderr.buffer)


def ctypes_int(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def main():
    a = sys.argv[1:]
    v = False
    if a and a[0] == "-v":
        v, a = True, a[1:]
    if not a:
        sys.exit(__doc__)
    sys.exit(Runner(a[0], a[1:], v).run() & 0xFF)


if __name__ == "__main__":
    main()
