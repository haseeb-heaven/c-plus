#!/usr/bin/env python3
"""Real-world-style native ABI regression suite for the C+ compiler.

The subject under test is the *native compiler binary* (``$CPLUS_BIN``,
default ``$PWD/build/c+``).  Every fixture is an idiomatic C program that
exercises a facet of the platform ABI: pointer widths, aggregate layout,
bit-field packing, enum representation, function pointers, callbacks,
variadic calls, cross-translation-unit signatures, exported symbols and
static archive interop.

For every fixture the suite also compiles and runs the *same source* with
``/usr/bin/clang`` under ``-std=gnu17`` and ``-std=c99``.  Clang is the
reference implementation: both oracle runs must produce the pinned, exact
stdout, and must agree with each other.  Every C+ build (``--legacy-c``,
``--default``, ``--strict``, always ``--sanitize=none``) must then match
that reference exactly -- full-string stdout equality, return code 0, and
(for symbol cases) exact ``nm`` symbol sets.  Nothing is matched by
substring and no subprocess failure is hidden with ``|| true``.

Results (under ``results/``):

* ``summary.txt``  -- totals, per-mode/per-family breakdown, every failure
* ``summary.json`` -- full machine-readable record, including both oracle
  runs, all build/run/nm/ar logs, expected vs actual stdout and symbol sets
* ``logs/<case>.<tag>.*.txt`` -- complete subprocess logs
* ``cases/``       -- deterministically regenerated fixtures and binaries
"""

from __future__ import annotations

import concurrent.futures
import dataclasses
import datetime
import json
import os
import pathlib
import platform as platform_mod
import re
import shutil
import subprocess
import sys
from collections import Counter
from typing import Optional

HERE = pathlib.Path(__file__).resolve().parent
CASES_DIR = HERE / "cases"
RESULTS = HERE / "results"
LOGS = RESULTS / "logs"

BIN = os.environ.get("CPLUS_BIN", str(pathlib.Path.cwd() / "build" / "c+"))
CLANG = os.environ.get("ABI_CLANG", "/usr/bin/clang")
MODES = ("--legacy-c", "--default", "--strict")
ORACLE_STDS = ("-std=gnu17", "-std=c99")
TIMEOUT = int(os.environ.get("ABI_TIMEOUT", "90"))
RUN_TIMEOUT = min(30, TIMEOUT)
WORKERS = int(os.environ.get("ABI_WORKERS", "4"))
SANITIZE = "--sanitize=none"  # sanitizers perturb the ABI under measurement

NM = shutil.which("nm")
AR = shutil.which("ar")

# Pinned LP64 values.  These are explicit constants, never values read back
# from the compiler under test.  They render both the fixtures and the exact
# expected stdout.  Only little-endian LP64 hosts are supported; an unknown
# host is a hard configuration error, not a silent skip.
LP64_ABI = {
    "char": 1,
    "short": 2,
    "int": 4,
    "long": 8,
    "llong": 8,
    "float": 4,
    "double": 8,
    "ptr": 8,
    "uintptr": 8,
    "size": 8,
    "enum_size": 4,
    "mixed_size": 24,
    "mixed_tag": 0,
    "mixed_seq": 4,
    "mixed_stamp": 8,
    "mixed_flags": 16,
    "wire_size": 13,
    "wire_tag": 0,
    "wire_seq": 1,
    "wire_stamp": 5,
    "seg_size": 24,
    "seg_id": 0,
    "seg_a": 4,
    "seg_b": 12,
    "seg_weight": 20,
    "seg_ax": 4,
    "seg_ay": 8,
    "seg_bx": 12,
    "seg_by": 16,
    "samples_size": 40,
    "samples_id": 0,
    "samples_values": 8,
    "samples_scale": 32,
    "flags_size": 4,
    "reg_size": 8,
    "big_size": 32,
}

SUPPORTED = {
    ("Darwin", "arm64"),
    ("Darwin", "x86_64"),
    ("Linux", "x86_64"),
    ("Linux", "aarch64"),
}


def platform_abi():
    system = platform_mod.system()
    machine = platform_mod.machine().lower()
    if (system, machine) not in SUPPORTED:
        raise SystemExit(
            "unsupported ABI platform %s/%s; extend LP64_ABI and SUPPORTED "
            "with exact constants rather than skipping" % (system, machine)
        )
    return dict(LP64_ABI), ("_" if system == "Darwin" else "")


ABI, SYM_PREFIX = platform_abi()

_TOKEN = re.compile(r"\{([a-z_]+)\}")


def render(text):
    """Substitute only known ABI tokens; C braces are left untouched."""
    return _TOKEN.sub(lambda m: str(ABI[m.group(1)])
                      if m.group(1) in ABI else m.group(0), text)


# ---------------------------------------------------------------------------
# Fixtures.  All sources are gnu89-clean (declarations before statements, no
# for-loop declarations) so --legacy-c, which lowers with -std=gnu89, builds
# the very same source as clang -std=gnu17/-std=c99.
# ---------------------------------------------------------------------------

STDIO = "#include <stdio.h>\n"
STDDET = "#include <stddef.h>\n"
STDINT = "#include <stdint.h>\n"
STRING = "#include <string.h>\n"

POINTER_SIZES = STDIO + STDINT + STDDET + r"""
/* Real-world style: ABI width probe used by a serialization layer. */
typedef void (*VoidFn)(void);
typedef int (*IntFn)(int);
static void rw_noop(void) { }
static int rw_identity(int x) { int y = x; return y; }
int main(void) {
  void *obj = 0;
  int *ip = 0;
  VoidFn vf = rw_noop;
  IntFn iff = rw_identity;
  uintptr_t up = 0;
  printf("ptr=%zu,obj=%zu,ip=%zu,fn=%zu,vfn=%zu,up=%zu,st=%zu\n",
         sizeof(void *), sizeof(obj), sizeof(ip), sizeof(iff), sizeof(vf),
         sizeof(uintptr_t), sizeof(size_t));
  printf("null=%d,call=%d\n",
         (obj == 0) && (ip == 0) && (up == 0), iff(7));
  return 0;
}
"""

STRUCT_LAYOUT = STDIO + STDDET + r"""
/* Real-world style: packet header, packed wire header, nested point and
   sampled-readings record. */
struct Header { unsigned char tag; int seq; double stamp; short flags; };
#pragma pack(push, 1)
struct WireHeader { unsigned char tag; int seq; double stamp; };
#pragma pack(pop)
struct Point { int x; int y; };
struct Segment {
  unsigned char id; struct Point a; struct Point b; short weight;
};
struct Samples { unsigned char id; long values[3]; double scale; };
int main(void) {
  printf("header=%zu,%zu,%zu,%zu,%zu\n", sizeof(struct Header),
         offsetof(struct Header, tag), offsetof(struct Header, seq),
         offsetof(struct Header, stamp), offsetof(struct Header, flags));
  printf("wire=%zu,%zu,%zu,%zu\n", sizeof(struct WireHeader),
         offsetof(struct WireHeader, tag), offsetof(struct WireHeader, seq),
         offsetof(struct WireHeader, stamp));
  printf("seg=%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n",
         sizeof(struct Segment), offsetof(struct Segment, id),
         offsetof(struct Segment, a), offsetof(struct Segment, b),
         offsetof(struct Segment, weight), offsetof(struct Segment, a.x),
         offsetof(struct Segment, a.y), offsetof(struct Segment, b.x),
         offsetof(struct Segment, b.y));
  printf("samples=%zu,%zu,%zu,%zu\n", sizeof(struct Samples),
         offsetof(struct Samples, id), offsetof(struct Samples, values),
         offsetof(struct Samples, scale));
  return 0;
}
"""

UNION_LAYOUT = STDIO + STRING + r"""
/* Real-world style: a tagged variant carrying an integer or a double. */
union Value { long i; double d; unsigned char bytes[16]; };
int main(void) {
  union Value v;
  size_t k;
  memset(&v, 0, sizeof v); v.d = 2.5;
  printf("size=%zu d=%.1f:", sizeof(union Value), v.d);
  for (k = 0; k < sizeof v.bytes; k++) printf("%02x", v.bytes[k]);
  printf("\n");
  memset(&v, 0, sizeof v); v.i = -7L;
  printf("i=%ld:", v.i);
  for (k = 0; k < sizeof v.bytes; k++) printf("%02x", v.bytes[k]);
  printf("\n");
  return 0;
}
"""

BITFIELD_LAYOUT = STDIO + STRING + r"""
/* Real-world style: packed device registers described with bit-fields
   (offsetof is not defined for bit-fields, so the raw bytes are inspected). */
struct Flags { unsigned a:3; unsigned b:5; unsigned c:8; unsigned d:16; };
struct Reg { unsigned a:3; unsigned b:5; unsigned :0; unsigned c:9; int d:7; };
int main(void) {
  struct Flags f;
  unsigned char raw[sizeof(struct Flags)];
  size_t i;
  memset(&f, 0, sizeof f); f.a = 5; f.b = 17; f.c = 0xA5; f.d = 0x1234;
  memcpy(raw, &f, sizeof raw);
  printf("flags=%zu:", sizeof(struct Flags));
  for (i = 0; i < sizeof raw; i++) printf("%02x", raw[i]);
  printf("\n");
  memset(&f, 0, sizeof f); f.a = 1; f.d = 7;
  memcpy(raw, &f, sizeof raw);
  printf("sparse=%zu:", sizeof(struct Flags));
  for (i = 0; i < sizeof raw; i++) printf("%02x", raw[i]);
  printf("\n");
  {
    struct Reg r; unsigned char rraw[sizeof(struct Reg)];
    memset(&r, 0, sizeof r); r.a = 5; r.b = 17; r.c = 0x101; r.d = -3;
    memcpy(rraw, &r, sizeof rraw);
    printf("reg=%zu:", sizeof(struct Reg));
    for (i = 0; i < sizeof rraw; i++) printf("%02x", rraw[i]);
    printf(",%u,%u,%u,%d\n", r.a, r.b, r.c, r.d);
  }
  return 0;
}
"""

ENUM_SIZES = STDIO + STRING + r"""
/* Real-world style: small, full-range and flag enumerations persisted to a
   binary format. */
enum Small { S_ZERO = 0, S_SEVEN = 7 };
enum Wide { W_MAX = 2147483647, W_NEG = -1 };
enum Flag { F_ONE = 1, F_BIG = 2147483646 };
int main(void) {
  enum Small s = S_SEVEN;
  enum Wide w = W_MAX;
  unsigned char raw[sizeof(enum Small)];
  size_t i;
  memcpy(raw, &s, sizeof raw);
  printf("e=%zu,%zu,%zu v=%d,%d,%d raw=", sizeof(enum Small),
         sizeof(enum Wide), sizeof(enum Flag), (int)s, (int)w, (int)F_BIG);
  for (i = 0; i < sizeof raw; i++) printf("%02x", raw[i]);
  printf("\n");
  return 0;
}
"""

FNPTR_REPRESENTATION = STDIO + STDINT + STRING + r"""
/* Real-world style: dispatch table entries compared by address and by raw
   representation, then stored in and returned from a holder struct. */
typedef int (*Fn)(int, int);
struct Holder { Fn f; long tag; };
static int rw_target(int a, int b) { int t = a * 10; int u = t + b; return u; }
static int rw_other(int a, int b) { int t = a - b; return t; }
static Fn rw_roundtrip(Fn f) { struct Holder h; h.f = f; h.tag = 9; return h.f; }
int main(void) {
  Fn a = rw_target, b = &rw_target, c = rw_other;
  uintptr_t ua = 0, ub = 0, uc = 0;
  memcpy(&ua, &a, sizeof a); memcpy(&ub, &b, sizeof b);
  memcpy(&uc, &c, sizeof c);
  printf("sz=%zu,%zu ua=%zu eq=%d same=%d diff=%d call=%d rt=%d\n",
         sizeof(Fn), sizeof(void *), sizeof(uintptr_t), a == b ? 1 : 0,
         ua == ub ? 1 : 0, ua != uc ? 1 : 0, a(4, 2), rw_roundtrip(a)(5, 3));
  return 0;
}
"""

CALLBACK_CONVENTIONS = STDIO + r"""
/* Real-world style: callbacks with int, double and struct-returning
   signatures, invoked through parameters and an operations table. */
struct Pair { long x; double y; };
static int cb_add(int a, int b) { int t = a + b; return t; }
static double cb_mul(double a, double b) { double t = a * b; return t; }
static struct Pair cb_pair(long x, double y) {
  struct Pair p; p.x = x; p.y = y; return p;
}
static int invoke2(int (*f)(int, int), int a, int b) { int t = f(a, b); return t; }
static double invoke_dbl(double (*f)(double, double), double a, double b) {
  double t = f(a, b); return t;
}
static struct Pair invoke_pair(struct Pair (*f)(long, double), long a, double b) {
  struct Pair p = f(a, b); return p;
}
struct Ops { int (*add)(int, int); double (*mul)(double, double); };
int main(void) {
  struct Pair p; struct Ops ops;
  p = invoke_pair(cb_pair, 7, 2.5);
  ops.add = cb_add; ops.mul = cb_mul;
  printf("cb=%d,%.1f,%ld,%.1f,struct=%d,%.1f\n",
         invoke2(cb_add, 20, 22), invoke_dbl(cb_mul, 1.5, 4.0),
         p.x, p.y, ops.add(3, 4), ops.mul(2.0, 3.0));
  return 0;
}
"""

QSORT_CALLBACKS = STDIO + "#include <stdlib.h>\n" + r"""
/* Real-world style: qsort comparators for scalar and record arrays. */
typedef struct { int key; const char *name; } Item;
static int cmp_item(const void *a, const void *b) {
  int x = ((const Item *)a)->key; int y = ((const Item *)b)->key;
  if (x > y) return 1; if (x < y) return -1; return 0;
}
static int cmp_int(const void *a, const void *b) {
  int x = *(const int *)a; int y = *(const int *)b; return (x > y) - (x < y);
}
int main(void) {
  Item items[4]; int nums[5];
  items[0].key = 3; items[0].name = "c";
  items[1].key = 1; items[1].name = "a";
  items[2].key = 4; items[2].name = "d";
  items[3].key = 2; items[3].name = "b";
  qsort(items, 4, sizeof(Item), cmp_item);
  nums[0] = 5; nums[1] = 3; nums[2] = 9; nums[3] = 1; nums[4] = 4;
  qsort(nums, 5, sizeof(int), cmp_int);
  printf("items=%s%s%s%s nums=%d%d%d%d%d\n",
         items[0].name, items[1].name, items[2].name, items[3].name,
         nums[0], nums[1], nums[2], nums[3], nums[4]);
  return 0;
}
"""

VARIADIC_ABI = STDIO + "#include <stdarg.h>\n" + r"""
/* Real-world style: variadic helpers plus a libc variadic call. */
static long long describe(const char *tag, ...) {
  va_list ap; int i; double d; long l; const char *s;
  va_start(ap, tag);
  i = va_arg(ap, int); d = va_arg(ap, double);
  l = va_arg(ap, long); s = va_arg(ap, const char *);
  va_end(ap);
  printf("d=%s,%d,%.2f,%ld,%s\n", tag, i, d, l, s);
  return (long long)i + (long long)l;
}
static int eight(int n, ...) {
  va_list ap; int total = 0, k;
  va_start(ap, n);
  for (k = 0; k < n; k++) total += va_arg(ap, int);
  va_end(ap); return total;
}
int main(void) {
  char buf[64]; int n;
  printf("sum=%d,%d\n", eight(8, 1, 2, 3, 4, 5, 6, 7, 8), eight(1, 42));
  printf("ret=%lld\n", describe("v", 7, 2.5, 100L, "ok"));
  n = snprintf(buf, sizeof buf, "fmt=%d/%.1f/%s", 3, 1.5, "z");
  printf("snp=%d:%s\n", n, buf);
  return 0;
}
"""

EXTERN_MAIN = STDIO + r"""
/* Real-world style: a consumer TU with extern declarations whose definitions
   live in another translation unit (int/long/double/pointer/struct returns). */
struct Pair { long x; double y; };
extern int ext_add(int, int);
extern long ext_long(long, long, long);
extern double ext_double(double, double);
extern int ext_ptr(const int *);
extern struct Pair ext_pair(long, double);
static int hidden_add(int a, int b) { int t = a - b; return t; }
int main(void) {
  struct Pair p; int v = 5;
  p = ext_pair(7, 2.5);
  printf("x=%d,%ld,%.1f,%d,%ld,%.1f,h=%d\n",
         ext_add(20, 22), ext_long(1, 2, 3), ext_double(1.5, 4.0),
         ext_ptr(&v), p.x, p.y, hidden_add(9, 2));
  return 0;
}
"""

EXTERN_IMPL = r"""
struct Pair { long x; double y; };
int ext_add(int a, int b) { int t = a + b; return t; }
long ext_long(long a, long b, long c) { long t = a + b; long u = t + c; return u; }
double ext_double(double a, double b) { double t = a * b; return t; }
int ext_ptr(const int *p) { return *p; }
struct Pair ext_pair(long x, double y) { struct Pair p; p.x = x; p.y = y; return p; }
"""

EXPORTED_SYMBOLS = STDIO + r"""
/* Real-world style: a module exporting functions and globals while keeping
   its helpers static. */
int rw_glob_counter = 41;
const char *rw_glob_name = "rw";
int rw_add_exported(int a, int b) { int t = a + b; return t; }
static int rw_add_hidden(int a, int b) { int t = a - b; return t; }
int main(void) {
  rw_glob_counter = 42;
  printf("sym=%d,%d,%s,%d\n", rw_add_exported(3, 4), rw_add_hidden(9, 2),
         rw_glob_name, rw_glob_counter);
  return 0;
}
"""

LIB_HELPER = r"""
struct LibPair { long a; double b; };
int lib_mul(int a, int b) { int t = a * b; return t; }
double lib_scale(double x) { double t = x * 2.0; return t; }
struct LibPair lib_pair(long a, double b) {
  struct LibPair p; p.a = a; p.b = b; return p;
}
int lib_apply(int (*fn)(int, int), int a, int b) { int t = fn(a, b); return t; }
"""

LIB_MAIN = STDIO + r"""
/* Real-world style: link against a separately compiled static library whose
   code was built by the same toolchain under test. */
struct LibPair { long a; double b; };
extern int lib_mul(int, int);
extern double lib_scale(double);
extern struct LibPair lib_pair(long, double);
extern int lib_apply(int (*fn)(int, int), int, int);
static int cb(int a, int b) { int t = a + b; return t; }
int main(void) {
  struct LibPair p;
  p = lib_pair(6, 7);
  printf("lib=%d,%.1f,%ld,%.1f,cb=%d\n",
         lib_mul(6, 7), lib_scale(1.25), p.a, p.b, lib_apply(cb, 4, 5));
  return 0;
}
"""


@dataclasses.dataclass(frozen=True)
class Case:
    name: str
    family: str
    kind: str  # "run" | "symbols" | "lib"
    source: str
    stdout: str
    note: str = ""
    helper_source: str = ""
    object_symbols: tuple = ()        # exact object globals (logical names)
    binary_own_symbols: tuple = ()    # exact module-prefixed binary globals
    forbidden_symbols: tuple = ()     # must NOT be a defined global
    undefined_symbols: tuple = ()     # must appear in nm -u
    helper_symbols: tuple = ()        # exact helper archive globals


CASES: list[Case] = [
    Case("pointer_sizes", "pointer_size", "run", POINTER_SIZES,
         "ptr={ptr},obj={ptr},ip={ptr},fn={ptr},vfn={ptr},up={uintptr},st={size}\n"
         "null=1,call=7\n",
         note="sizeof(void*), object/function pointers, uintptr_t, null checks"),
    Case("struct_layout", "struct_layout", "run", STRUCT_LAYOUT,
         "header={mixed_size},{mixed_tag},{mixed_seq},{mixed_stamp},{mixed_flags}\n"
         "wire={wire_size},{wire_tag},{wire_seq},{wire_stamp}\n"
         "seg={seg_size},{seg_id},{seg_a},{seg_b},{seg_weight},"
         "{seg_ax},{seg_ay},{seg_bx},{seg_by}\n"
         "samples={samples_size},{samples_id},{samples_values},{samples_scale}\n",
         note="sizeof/offsetof for normal, packed, nested and array structs"),
    Case("union_layout", "struct_layout", "run", UNION_LAYOUT,
         "size=16 d=2.5:00000000000004400000000000000000\n"
         "i=-7:f9ffffffffffffff0000000000000000\n",
         note="union size and raw byte representation of each active member"),
    Case("bitfield_layout", "struct_layout", "run", BITFIELD_LAYOUT,
         "flags={flags_size}:8da53412\nsparse={flags_size}:01000700\n"
         "reg={reg_size}:8d00000001fb0000,5,17,257,-3\n",
         note="bit-field packing incl. a zero-width separator, via raw bytes"),
    Case("enum_sizes", "enum", "run", ENUM_SIZES,
         "e={enum_size},{enum_size},{enum_size} "
         "v=7,2147483647,2147483646 raw=07000000\n",
         note="size and representation of small, full-range and flag enums"),
    Case("fnptr_representation", "function_pointer", "run", FNPTR_REPRESENTATION,
         "sz={ptr},{ptr} ua={uintptr} eq=1 same=1 diff=1 call=42 rt=53\n",
         note="function pointer equality, byte representation and struct round-trip"),
    Case("callback_conventions", "callback", "run", CALLBACK_CONVENTIONS,
         "cb=42,6.0,7,2.5,struct=7,6.0\n",
         note="int/double/struct-returning callbacks through pointers and a table"),
    Case("qsort_callbacks", "callback", "run", QSORT_CALLBACKS,
         "items=abcd nums=13459\n",
         note="qsort comparator callbacks for scalar and record arrays"),
    Case("variadic_abi", "variadic", "run", VARIADIC_ABI,
         "sum=36,42\nd=v,7,2.50,100,ok\nret=107\nsnp=11:fmt=3/1.5/z\n",
         note="va_arg for int/double/long/pointer and a libc variadic call"),
    Case("extern_signatures", "extern_signature", "lib", EXTERN_MAIN,
         "x=42,6,6.0,5,7,2.5,h=7\n",
         helper_source=EXTERN_IMPL,
         helper_symbols=("ext_add", "ext_double", "ext_long", "ext_pair", "ext_ptr"),
         note="extern scalar/pointer/struct signatures across translation units"),
    Case("exported_symbols", "symbol_names", "symbols", EXPORTED_SYMBOLS,
         "sym=7,7,rw,42\n",
         object_symbols=("main", "rw_add_exported", "rw_glob_counter", "rw_glob_name"),
         binary_own_symbols=("rw_add_exported", "rw_glob_counter", "rw_glob_name"),
         forbidden_symbols=("rw_add_hidden",),
         undefined_symbols=("printf",),
         note="exact defined/undefined C symbol names via nm on object and binary"),
    Case("static_library_interop", "library_interop", "lib", LIB_MAIN,
         "lib=42,2.5,6,7.0,cb=9\n",
         helper_source=LIB_HELPER,
         helper_symbols=("lib_apply", "lib_mul", "lib_pair", "lib_scale"),
         note="static archive built by the same compiler: exact names, then linked"),
]


# ---------------------------------------------------------------------------
# Subprocess helpers -- every return code is classified; no `|| true`.
# ---------------------------------------------------------------------------


def run_cmd(argv, timeout=TIMEOUT):
    try:
        p = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        return {"rc": p.returncode, "stdout": p.stdout, "stderr": p.stderr,
                "timed_out": False, "command": list(map(str, argv))}
    except subprocess.TimeoutExpired as exc:
        out = exc.stdout or b""
        err = exc.stderr or b""
        if isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        if isinstance(err, bytes):
            err = err.decode("utf-8", "replace")
        return {"rc": None, "stdout": out, "stderr": err, "timed_out": True,
                "command": list(map(str, argv))}
    except OSError as exc:
        return {"rc": None, "stdout": "", "stderr": "OS ERROR: %s" % exc,
                "timed_out": False, "command": list(map(str, argv))}


def log_text(res):
    return ("command: %s\nreturncode: %s\ntimed_out: %s\n\n"
            "--- stdout ---\n%s\n--- stderr ---\n%s\n"
            % (" ".join(res["command"]), res["rc"], res["timed_out"],
               res["stdout"], res["stderr"]))


def write_log(name, suffix, res):
    (LOGS / ("%s.%s" % (name, suffix))).write_text(log_text(res))


def mode_tag(mode):
    return mode.lstrip("-").replace("-", "_")


def std_tag(std):
    # Must not contain '=': ld does not resolve -l names containing it.
    return std.lstrip("-").replace("=", "_").replace("-", "_")


def nm_names(path, defined_only=True, undefined_only=False):
    argv = [NM]
    if defined_only:
        argv.extend(["-g", "--defined-only"])
    if undefined_only:
        argv.append("-u")
    argv.append(str(path))
    res = run_cmd(argv, timeout=30)
    names = []
    for line in res["stdout"].splitlines():
        text = line.strip()
        if not text:
            continue
        if undefined_only:
            names.append(text.split()[-1])
            continue
        parts = text.split()
        if len(parts) >= 3 and re.fullmatch(r"[0-9a-fA-F]+", parts[0]):
            names.append(parts[-1])
    return res, names


def strip_prefix(sym):
    if SYM_PREFIX and sym.startswith(SYM_PREFIX):
        return sym[len(SYM_PREFIX):]
    return sym


def build_ok(res, out):
    """Return a list of reasons why a build result cannot be used."""
    reasons = []
    if res["timed_out"]:
        reasons.append("build TIMEOUT")
    elif res["rc"] != 0:
        reasons.append("build failed (rc=%s)" % res["rc"])
    if not out.exists():
        reasons.append("no output produced at %s" % out.name)
    return reasons


def check_run_result(res, expected, label):
    reasons = []
    if res["timed_out"]:
        reasons.append("%s execution TIMEOUT after %ss" % (label, RUN_TIMEOUT))
    if res["rc"] != 0:
        reasons.append("%s rc=%s, expected 0" % (label, res["rc"]))
    if res["stdout"] != expected:
        reasons.append("%s stdout mismatch: expected %r, got %r"
                       % (label, expected, res["stdout"]))
    return reasons


# ---------------------------------------------------------------------------
# Build command construction
# ---------------------------------------------------------------------------


def cplus_build(src, mode, out, extra=()):
    argv = [BIN, "build", str(src), mode, SANITIZE]
    argv.extend(str(x) for x in extra)
    argv.extend(["-o", str(out)])
    return run_cmd(argv)


def cplus_obj(src, mode, out):
    argv = [BIN, "build", str(src), mode, SANITIZE, "-c", "-o", str(out)]
    return run_cmd(argv)


def clang_build(src, std, out, extra=()):
    argv = [CLANG, std, str(src)]
    argv.extend(str(x) for x in extra)
    argv.extend(["-o", str(out)])
    return run_cmd(argv)


def clang_obj(src, std, out):
    return run_cmd([CLANG, std, "-c", str(src), "-o", str(out)])


def archive(objs, lib):
    return run_cmd([AR, "rcs", str(lib)] + [str(o) for o in objs], timeout=30)


# ---------------------------------------------------------------------------
# Oracle: compile and run with clang -std=gnu17 and -std=c99.  Both runs must
# match the pinned stdout and the pinned symbol sets exactly.
# ---------------------------------------------------------------------------


def oracle_symbol_checks(case, tag, obj, binary, log_prefix):
    """Verify pinned symbol sets on clang-generated object/binary/archive."""
    reasons = []
    info = {"object": None, "binary": None, "undefined": None, "helper": None}
    if NM is None:
        return ["SKIP: nm not available on PATH"], info

    if case.kind in ("symbols",):
        if obj is not None and obj.exists():
            nr, names = nm_names(obj)
            logical = sorted(strip_prefix(n) for n in names)
            info["object"] = {"result": nr, "raw": sorted(names), "logical": logical}
            write_log(log_prefix, "oracle_obj_nm.txt", nr)
            if nr["rc"] != 0 or nr["timed_out"]:
                reasons.append("oracle nm -g on object failed (rc=%s)" % nr["rc"])
            elif set(logical) != set(case.object_symbols):
                reasons.append("oracle object globals: expected %s, got %s"
                               % (sorted(case.object_symbols), logical))
            for n in case.forbidden_symbols:
                if n in logical:
                    reasons.append("oracle forbidden local symbol %s is global" % n)
        if binary is not None and binary.exists():
            br, bnames = nm_names(binary)
            own = sorted(strip_prefix(n) for n in bnames
                         if strip_prefix(n).startswith("rw_"))
            info["binary"] = {"result": br, "raw_own": own, "logical_own": own}
            write_log(log_prefix, "oracle_bin_nm.txt", br)
            if br["rc"] != 0 or br["timed_out"]:
                reasons.append("oracle nm -g on binary failed (rc=%s)" % br["rc"])
            else:
                expected = sorted(case.binary_own_symbols)
                if own != expected:
                    reasons.append("oracle binary rw_* globals: expected %s, got %s"
                                   % (expected, own))
                for n in case.forbidden_symbols:
                    if n in own:
                        reasons.append("oracle forbidden local symbol %s exported" % n)
                if "main" not in [strip_prefix(n) for n in bnames]:
                    reasons.append("oracle main symbol missing from binary")
            ur, unames = nm_names(binary, defined_only=False, undefined_only=True)
            info["undefined"] = {"result": ur, "raw": sorted(set(unames))}
            write_log(log_prefix, "oracle_undef_nm.txt", ur)
            if ur["rc"] != 0 or ur["timed_out"]:
                reasons.append("oracle nm -u on binary failed (rc=%s)" % ur["rc"])
            else:
                logical_u = set(strip_prefix(n) for n in unames)
                for n in case.undefined_symbols:
                    if n not in logical_u:
                        reasons.append("oracle undefined symbol %s not found" % n)
    if case.kind == "lib" and case.helper_symbols:
        helper_archive = CASES_DIR / ("lib%s_oracle_%s.a" % (case.name, tag))
        if helper_archive.exists():
            hr, hnames = nm_names(helper_archive)
            logical = sorted(strip_prefix(n) for n in hnames)
            info["helper"] = {"result": hr, "raw": sorted(hnames), "logical": logical}
            write_log(log_prefix, "oracle_helper_nm.txt", hr)
            if hr["rc"] != 0 or hr["timed_out"]:
                reasons.append("oracle nm -g on archive failed (rc=%s)" % hr["rc"])
            elif set(logical) != set(case.helper_symbols):
                reasons.append("oracle archive globals: expected %s, got %s"
                               % (sorted(case.helper_symbols), logical))
    return reasons, info


def evaluate_oracle(case):
    """One oracle entry: both clang standards, with all logs and reasons."""
    expected = render(case.stdout)
    prefix = case.name + ".oracle"
    result = {"expected_stdout": expected, "stds": {}, "reasons": [],
              "symbol_sets": {}, "sources": {}}
    if not os.path.exists(CLANG):
        result["reasons"].append("SKIP: clang not found at %s" % CLANG)
        return result

    for std in ORACLE_STDS:
        tag = std_tag(std)
        lp = "%s.%s" % (prefix, tag)
        entry = {"std": std, "build": None, "run": None}
        reasons = []

        src = CASES_DIR / ("%s.oracle_%s.c" % (case.name, tag))
        src.write_text(render(case.source).lstrip("\n"))
        result["sources"][std] = src.read_text()
        binary = CASES_DIR / ("%s.oracle_%s.bin" % (case.name, tag))
        if binary.exists():
            binary.unlink()
        obj = CASES_DIR / ("%s.oracle_%s.o" % (case.name, tag))
        if obj.exists():
            obj.unlink()

        extra = []
        helper_obj = None
        archive_path = None
        if case.kind == "lib":
            if AR is None:
                entry["reasons"] = ["SKIP: ar not available on PATH"]
                result["stds"][std] = entry
                result["reasons"].extend(entry["reasons"])
                continue
            hsrc = CASES_DIR / ("%s.oracle_%s.helper.c" % (case.name, tag))
            hsrc.write_text(case.helper_source.lstrip("\n"))
            helper_obj = CASES_DIR / ("%s.oracle_%s.helper.o" % (case.name, tag))
            lib_name = "%s_oracle_%s" % (case.name, tag)
            archive_path = CASES_DIR / ("lib%s.a" % lib_name)
            for p in (helper_obj, archive_path):
                if p.exists():
                    p.unlink()
            hb = clang_obj(hsrc, std, helper_obj)
            entry["helper_build"] = hb
            write_log(lp, "oracle_helper_build.txt", hb)
            if build_ok(hb, helper_obj):
                reasons.append("oracle helper object build failed (rc=%s)" % hb["rc"])
            else:
                ab = archive([helper_obj], archive_path)
                entry["ar"] = ab
                write_log(lp, "oracle_ar.txt", ab)
                if build_ok(ab, archive_path):
                    reasons.append("oracle archive creation failed (rc=%s)" % ab["rc"])
                else:
                    extra = ["-L" + str(CASES_DIR), "-l" + lib_name]

        if not reasons:
            b = clang_build(src, std, binary, extra=extra)
            entry["build"] = b
            write_log(lp, "oracle_build.txt", b)
            reasons.extend(build_ok(b, binary))
            if not reasons:
                x = run_cmd([str(binary)], timeout=RUN_TIMEOUT)
                entry["run"] = x
                write_log(lp, "oracle_run.txt", x)
                reasons.extend(check_run_result(x, expected, "oracle"))

        if case.kind == "symbols" and not reasons:
            ob = clang_obj(src, std, obj)
            entry["object_build"] = ob
            write_log(lp, "oracle_obj_build.txt", ob)
            if build_ok(ob, obj):
                reasons.append("oracle object build failed (rc=%s)" % ob["rc"])
            else:
                sym_reasons, symbols = oracle_symbol_checks(
                    case, tag, obj, binary, lp)
                reasons.extend(sym_reasons)
                entry["symbols"] = symbols
                result["symbol_sets"][std] = symbols
        elif case.kind == "lib" and not reasons:
            sym_reasons, symbols = oracle_symbol_checks(case, tag, None, None, lp)
            reasons.extend(sym_reasons)
            entry["symbols"] = symbols
            result["symbol_sets"][std] = symbols

        entry["reasons"] = reasons
        result["stds"][std] = entry
        result["reasons"].extend(reasons)

    # Cross-check: the two oracle standards must agree on stdout.  Both are
    # already pinned, but this makes any std-sensitive fixture explicit.
    outs = [result["stds"].get(s, {}).get("run", {}) or {} for s in ORACLE_STDS]
    if all("stdout" in o for o in outs):
        if outs[0]["stdout"] != outs[1]["stdout"]:
            result["reasons"].append(
                "oracle std disagreement: gnu17=%r c99=%r"
                % (outs[0]["stdout"], outs[1]["stdout"]))
    return result


# ---------------------------------------------------------------------------
# C+ evaluation for one (case, mode)
# ---------------------------------------------------------------------------


def evaluate(case, mode, oracle):
    tag = mode_tag(mode)
    expected = render(case.stdout)
    rec = {
        "case": case.name,
        "family": case.family,
        "kind": case.kind,
        "mode": mode,
        "note": case.note,
        "expected_stdout": expected,
        "reasons": [],
        "oracle": oracle,
        "build": None,
        "run": None,
        "object": None,
        "lib": None,
        "symbols": None,
    }
    reasons = rec["reasons"]
    if any(r.startswith("SKIP:") for r in oracle["reasons"]):
        reasons.extend(oracle["reasons"])
        rec["status"] = "skip"
        return rec
    reasons.extend(oracle["reasons"])

    src = CASES_DIR / ("%s.%s.c" % (case.name, tag))
    src.write_text(render(case.source).lstrip("\n"))
    rec["source"] = src.read_text()
    binary = CASES_DIR / ("%s.%s.bin" % (case.name, tag))
    if binary.exists():
        binary.unlink()
    obj = CASES_DIR / ("%s.%s.o" % (case.name, tag))
    if obj.exists():
        obj.unlink()

    if case.kind == "symbols":
        if NM is None:
            reasons.append("SKIP: nm not available on PATH")
        else:
            ob = cplus_obj(src, mode, obj)
            rec["object"] = {"build": ob}
            write_log("%s.%s" % (case.name, tag), "obj_build.txt", ob)
            if build_ok(ob, obj):
                reasons.append("object build failed (rc=%s)" % ob["rc"])
            else:
                nr, names = nm_names(obj)
                logical = sorted(strip_prefix(n) for n in names)
                rec["object"]["nm"] = {"result": nr, "raw": sorted(names),
                                       "logical": logical}
                write_log("%s.%s" % (case.name, tag), "obj_nm.txt", nr)
                if nr["rc"] != 0 or nr["timed_out"]:
                    reasons.append("nm -g on object failed (rc=%s)" % nr["rc"])
                else:
                    expected_globals = sorted(case.object_symbols)
                    if logical != expected_globals:
                        reasons.append("object globals: expected exact set %s, got %s"
                                       % (expected_globals, logical))
                    oracle_globals = ((oracle.get("symbol_sets", {})
                                       .get(ORACLE_STDS[0], {})
                                       .get("object", {}) or {}).get("logical"))
                    if oracle_globals is not None and logical != sorted(oracle_globals):
                        reasons.append("object globals differ from clang oracle: "
                                       "clang=%s, c+=%s" % (sorted(oracle_globals), logical))
                    for n in case.forbidden_symbols:
                        if n in logical:
                            reasons.append("forbidden local symbol %s is global" % n)

    extra = []
    lib_ready = True
    if case.kind == "lib":
        if AR is None:
            reasons.append("SKIP: ar not available on PATH")
            lib_ready = False
        else:
            hsrc = CASES_DIR / ("%s.%s.helper.c" % (case.name, tag))
            hsrc.write_text(case.helper_source.lstrip("\n"))
            helper_obj = CASES_DIR / ("%s.%s.helper.o" % (case.name, tag))
            lib_name = "%s_%s" % (case.name, tag)
            archive_path = CASES_DIR / ("lib%s.a" % lib_name)
            for p in (helper_obj, archive_path):
                if p.exists():
                    p.unlink()
            hb = cplus_obj(hsrc, mode, helper_obj)
            info = {"helper_build": hb, "ar": None, "helper_nm": None}
            rec["lib"] = info
            write_log("%s.%s" % (case.name, tag), "helper_build.txt", hb)
            if build_ok(hb, helper_obj):
                reasons.append("helper object build failed (rc=%s)" % hb["rc"])
                lib_ready = False
            else:
                ab = archive([helper_obj], archive_path)
                info["ar"] = ab
                write_log("%s.%s" % (case.name, tag), "ar.txt", ab)
                if build_ok(ab, archive_path):
                    reasons.append("archive creation failed (rc=%s)" % ab["rc"])
                    lib_ready = False
                else:
                    extra = ["-L" + str(CASES_DIR), "-l" + lib_name]
                    if NM is not None:
                        hr, hnames = nm_names(archive_path)
                        logical = sorted(strip_prefix(n) for n in hnames)
                        info["helper_nm"] = {"result": hr, "raw": sorted(hnames),
                                             "logical": logical}
                        write_log("%s.%s" % (case.name, tag), "helper_nm.txt", hr)
                        if hr["rc"] != 0 or hr["timed_out"]:
                            reasons.append("nm -g on archive failed (rc=%s)" % hr["rc"])
                        else:
                            if set(logical) != set(case.helper_symbols):
                                reasons.append("archive globals: expected %s, got %s"
                                               % (sorted(case.helper_symbols), logical))
                            oracle_helpers = ((oracle.get("symbol_sets", {})
                                               .get(ORACLE_STDS[0], {})
                                               .get("helper", {}) or {}).get("logical"))
                            if (oracle_helpers is not None
                                    and set(logical) != set(oracle_helpers)):
                                reasons.append("archive globals differ from clang "
                                               "oracle: clang=%s, c+=%s"
                                               % (sorted(oracle_helpers), logical))

    if not lib_ready:
        rec["status"] = "skip" if any(r.startswith("SKIP:")
                                      for r in reasons) else "fail"
        return rec

    b = cplus_build(src, mode, binary, extra=extra)
    rec["build"] = b
    write_log("%s.%s" % (case.name, tag), "build.txt", b)
    reasons.extend(build_ok(b, binary))
    if not build_ok(b, binary):
        x = run_cmd([str(binary)], timeout=RUN_TIMEOUT)
        rec["run"] = x
        write_log("%s.%s" % (case.name, tag), "run.txt", x)
        reasons.extend(check_run_result(x, expected, "c+"))

    if case.kind == "symbols" and NM is not None and binary.exists():
        br, bnames = nm_names(binary)
        own = sorted(strip_prefix(n) for n in bnames
                     if strip_prefix(n).startswith("rw_"))
        info = {"result": br, "raw_own": own, "logical_own": own}
        rec["symbols"] = info
        write_log("%s.%s" % (case.name, tag), "bin_nm.txt", br)
        if br["rc"] != 0 or br["timed_out"]:
            reasons.append("nm -g on binary failed (rc=%s)" % br["rc"])
        else:
            expected_own = sorted(case.binary_own_symbols)
            if own != expected_own:
                reasons.append("binary rw_* globals: expected %s, got %s"
                               % (expected_own, own))
            oracle_own = ((oracle.get("symbol_sets", {})
                           .get(ORACLE_STDS[0], {})
                           .get("binary", {}) or {}).get("logical_own"))
            if oracle_own is not None and own != sorted(oracle_own):
                reasons.append("binary globals differ from clang oracle: "
                               "clang=%s, c+=%s" % (sorted(oracle_own), own))
            for n in case.forbidden_symbols:
                if n in own:
                    reasons.append("forbidden local symbol %s exported" % n)
            if "main" not in [strip_prefix(n) for n in bnames]:
                reasons.append("main symbol missing from binary")
            ur, unames = nm_names(binary, defined_only=False, undefined_only=True)
            info["undefined"] = {"result": ur, "raw": sorted(set(unames))}
            write_log("%s.%s" % (case.name, tag), "undef_nm.txt", ur)
            if ur["rc"] != 0 or ur["timed_out"]:
                reasons.append("nm -u on binary failed (rc=%s)" % ur["rc"])
            else:
                logical_u = set(strip_prefix(n) for n in unames)
                for n in case.undefined_symbols:
                    if n not in logical_u:
                        reasons.append("undefined symbol %s not found" % n)

    skip = any(r.startswith("SKIP:") for r in reasons)
    rec["status"] = "skip" if skip else ("pass" if not reasons else "fail")
    return rec


# ---------------------------------------------------------------------------
# Summary / reporting
# ---------------------------------------------------------------------------


def version_string(binary):
    res = run_cmd([binary, "--version"], timeout=15)
    return (res["stdout"] + res["stderr"]).strip().splitlines()[:1]


def write_summary(records, oracle_by_case):
    statuses = Counter(r["status"] for r in records)
    failures = [r for r in records if r["status"] == "fail"]
    skips = [r for r in records if r["status"] == "skip"]

    by_family = {}
    for fam in sorted({r["family"] for r in records}):
        sub = [r for r in records if r["family"] == fam]
        by_family[fam] = {
            "checks": len(sub),
            "pass": sum(1 for r in sub if r["status"] == "pass"),
            "fail": sum(1 for r in sub if r["status"] == "fail"),
            "skip": sum(1 for r in sub if r["status"] == "skip"),
        }
    by_mode = {}
    for mode in MODES:
        sub = [r for r in records if r["mode"] == mode]
        by_mode[mode] = {
            "checks": len(sub),
            "pass": sum(1 for r in sub if r["status"] == "pass"),
            "fail": sum(1 for r in sub if r["status"] == "fail"),
            "skip": sum(1 for r in sub if r["status"] == "skip"),
        }

    oracle_failures = {name: o["reasons"] for name, o in oracle_by_case.items()
                       if any(not r.startswith("SKIP:") for r in o["reasons"])}

    payload = {
        "suite": "abi_real_world",
        "generated_at": datetime.datetime.now().isoformat(timespec="seconds"),
        "binary": BIN,
        "binary_version": version_string(BIN),
        "oracle_binary": CLANG,
        "oracle_version": version_string(CLANG),
        "oracle_stds": list(ORACLE_STDS),
        "platform": {
            "system": platform_mod.system(),
            "machine": platform_mod.machine(),
            "symbol_prefix": SYM_PREFIX,
            "abi_expectations": ABI,
        },
        "modes": list(MODES),
        "sanitize": SANITIZE,
        "timeout_seconds": TIMEOUT,
        "run_timeout_seconds": RUN_TIMEOUT,
        "workers": WORKERS,
        "tools": {"nm": NM, "ar": AR, "clang": CLANG},
        "totals": {
            "cases": len(CASES),
            "checks": len(records),
            "passed": statuses["pass"],
            "failed": statuses["fail"],
            "skipped": statuses["skip"],
            "oracle_runs": len(CASES) * len(ORACLE_STDS),
            "oracle_failed_cases": len(oracle_failures),
        },
        "by_family": by_family,
        "by_mode": by_mode,
        "oracle": oracle_by_case,
        "oracle_failures": oracle_failures,
        "failures": [
            {"case": r["case"], "family": r["family"], "mode": r["mode"],
             "reasons": r["reasons"],
             "expected_stdout": r["expected_stdout"],
             "actual_stdout": (r.get("run") or {}).get("stdout")}
            for r in failures
        ],
        "skips": [
            {"case": r["case"], "mode": r["mode"], "reasons": r["reasons"]}
            for r in skips
        ],
        "records": records,
    }
    (RESULTS / "summary.json").write_text(json.dumps(payload, indent=2) + "\n")

    lines = []
    add = lines.append
    add("C+ real-world-style native ABI regression suite")
    add("=" * 70)
    add("binary       : %s" % BIN)
    add("version      : %s" % ((payload["binary_version"] or ["(unknown)"])[0]))
    add("oracle       : %s (%s)" % (
        CLANG, (payload["oracle_version"] or ["(unknown)"])[0]))
    add("oracle stds  : %s" % ", ".join(ORACLE_STDS))
    add("platform     : %s/%s (symbol prefix %r)"
        % (platform_mod.system(), platform_mod.machine(), SYM_PREFIX))
    add("generated    : %s" % payload["generated_at"])
    add("modes        : %s" % ", ".join(MODES))
    add("backend flags: %s (no sanitizer instrumentation; ABI only)" % SANITIZE)
    add("timeout      : %ss build/nm, %ss execution" % (TIMEOUT, RUN_TIMEOUT))
    t = payload["totals"]
    add("")
    add("SUMMARY")
    add("-" * 70)
    add("cases                 : %d" % t["cases"])
    add("checks (case x mode)  : %d" % t["checks"])
    add("passed                : %d" % t["passed"])
    add("failed                : %d" % t["failed"])
    add("skipped               : %d" % t["skipped"])
    add("oracle runs           : %d (%d cases x %d stds)"
        % (t["oracle_runs"], t["cases"], len(ORACLE_STDS)))
    add("oracle failed cases   : %d" % t["oracle_failed_cases"])
    add("")
    add("BY MODE")
    add("-" * 70)
    for mode, info in by_mode.items():
        add("  %-10s checks=%2d passed=%2d failed=%2d skipped=%2d"
            % (mode, info["checks"], info["pass"], info["fail"], info["skip"]))
    add("")
    add("BY FAMILY")
    add("-" * 70)
    for fam, info in by_family.items():
        add("  %-20s checks=%2d passed=%2d failed=%2d skipped=%2d"
            % (fam, info["checks"], info["pass"], info["fail"], info["skip"]))
    add("")
    add("ORACLE (clang reference, exact stdout)")
    add("-" * 70)
    for case in CASES:
        o = oracle_by_case[case.name]
        ok = not any(not r.startswith("SKIP:") for r in o["reasons"])
        add("  %-24s %-4s %s" % (case.name, "OK" if ok else "FAIL",
                                 repr(o["expected_stdout"])))
        for reason in o["reasons"]:
            add("      - %s" % reason)
    add("")
    add("FAILURES (exact case names and modes)")
    add("-" * 70)
    if not failures:
        add("  none")
    else:
        for f in failures:
            add("  %-28s mode=%-10s family=%s"
                % (f["case"], f["mode"], f["family"]))
            for reason in f["reasons"]:
                add("      - %s" % reason)
            actual = (f.get("run") or {}).get("stdout")
            if actual is not None:
                add("      expected=%r" % f["expected_stdout"])
                add("      actual  =%r" % actual)
    add("")
    add("SKIPS")
    add("-" * 70)
    if not skips:
        add("  none")
    else:
        for s in skips:
            add("  %s [%s]: %s" % (s["case"], s["mode"], "; ".join(s["reasons"])))
    add("")
    add("Full records: results/summary.json")
    (RESULTS / "summary.txt").write_text("\n".join(lines) + "\n")
    return payload


def main():
    if not os.path.exists(BIN):
        print("error: CPLUS_BIN not found: %s" % BIN, file=sys.stderr)
        return 2
    CASES_DIR.mkdir(parents=True, exist_ok=True)
    LOGS.mkdir(parents=True, exist_ok=True)

    names = [c.name for c in CASES]
    if len(names) != len(set(names)):
        raise SystemExit("duplicate case name in manifest")

    print("abi_real_world: %d cases x %d modes, binary=%s"
          % (len(CASES), len(MODES), BIN))
    print("oracle: %s, stds=%s, platform=%s/%s"
          % (CLANG, ", ".join(ORACLE_STDS), platform_mod.system(),
             platform_mod.machine()))
    print("tools: nm=%s ar=%s" % (NM or "UNAVAILABLE", AR or "UNAVAILABLE"))

    # Oracle runs once per case (not once per C+ mode) so both standards are a
    # stable reference for every mode.
    oracle_by_case = {}
    for case in CASES:
        oracle_by_case[case.name] = evaluate_oracle(case)
        bad = [r for r in oracle_by_case[case.name]["reasons"]
               if not r.startswith("SKIP:")]
        print("  oracle %-24s %s" % (case.name, "OK" if not bad else "FAIL"))
        for reason in bad:
            print("      - %s" % reason)

    work = [(case, mode) for case in CASES for mode in MODES]
    records = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        futures = [pool.submit(evaluate, case, mode, oracle_by_case[case.name])
                   for case, mode in work]
        for future in concurrent.futures.as_completed(futures):
            records.append(future.result())
    records.sort(key=lambda r: (r["case"], MODES.index(r["mode"])))

    payload = write_summary(records, oracle_by_case)
    t = payload["totals"]
    print("checks: %d passed / %d failed / %d skipped"
          % (t["passed"], t["failed"], t["skipped"]))
    if payload["failures"]:
        print("failures (%d):" % t["failed"])
        for f in payload["failures"]:
            print("  - %s [%s/%s]" % (f["case"], f["mode"], f["family"]))
            for reason in f["reasons"]:
                print("      %s" % reason)
    if payload["skips"]:
        print("skips (%d):" % t["skipped"])
        for s in payload["skips"]:
            print("  - %s [%s]: %s" % (s["case"], s["mode"], s["reasons"]))
    print("summary: %s" % (RESULTS / "summary.txt"))
    return 1 if payload["failures"] or payload["oracle_failures"] else 0


if __name__ == "__main__":
    sys.exit(main())
