#!/usr/bin/env python3
"""Native ABI regression suite for the C+ compiler.

This suite treats the *native compiler binary* (``$CPLUS_BIN``, default
``$PWD/build/c+``) as the subject under test.  For every fixture it compiles a
small C program with the real backend, runs the produced Mach-O/ELF executable
and compares the *entire* stdout against an exact, platform-derived string.
It never uses substring matching for program output and never hides a
subprocess failure with ``|| true``: each return code, timeout and missing
output file is classified explicitly and recorded.

Coverage
--------
* ``pointer_size``       sizeof(void*), object/function pointers, scalar sizes
* ``struct_layout``      sizeof/offsetof for normal, packed, nested and
                         array-containing structs, unions and enums
* ``signature``          scalar/pointer/struct parameter and return passing
* ``calling_convention`` typed callbacks and a qsort comparator callback
* ``function_pointer``   pointer equality, byte-representation equality and
                         function pointers stored in / returned from structs
* ``symbol_names``       C symbol names on generated objects and executables
                         via ``nm`` (global exports and absence of locals)
* ``library_interop``    separately compiled static archive produced by the
                         same compiler, plus the compiler's shipped
                         precompiled runtime library

Modes
-----
Every case is compiled in ``--legacy-c``, ``--default`` and ``--strict``.  All
builds use ``--sanitize=none`` deliberately: sanitizer instrumentation and the
runtime recompilation it triggers are orthogonal to the emitted data layout
and calling convention, and instrumented builds would perturb the very ABI
this suite measures.  The C+ front-end mode flags are still applied in full.

Results (under ``results/``)
----------------------------
``summary.txt``            counts, per-family/per-mode breakdown, failures
``summary.json``           full machine-readable record, incl. all outputs
``logs/<case>.<tag>.*.txt``  build / run / nm / ar logs for every check
``cases/*.c``              deterministically regenerated fixtures
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
MODES = ("--legacy-c", "--default", "--strict")
TIMEOUT = int(os.environ.get("ABI_TIMEOUT", "90"))
RUN_TIMEOUT = min(30, TIMEOUT)
WORKERS = int(os.environ.get("ABI_WORKERS", "4"))
SANITIZE = "--sanitize=none"  # see module docstring

NM = shutil.which("nm")
AR = shutil.which("ar")

# Exact LP64 values expected from the host toolchain.  The suite refuses to
# guess: an unrecognised platform is a hard configuration error (exit 2), not
# a silent skip.  Values are used both to render the fixture and to render the
# expected stdout, so the assertions are explicit constants, not values read
# back from the compiler.
LP64_ABI = {
    "char": 1,
    "short": 2,
    "int": 4,
    "long": 8,
    "llong": 8,
    "float": 4,
    "double": 8,
    "ptr": 8,
    "fp": 8,
    "uintptr": 8,
    "mixed_size": 24,
    "mixed_a": 0,
    "mixed_b": 4,
    "mixed_c": 8,
    "mixed_d": 16,
    "packed_size": 13,
    "packed_a": 0,
    "packed_b": 1,
    "packed_c": 5,
    "outer_size": 16,
    "outer_a": 0,
    "outer_in": 4,
    "outer_b": 12,
    "outer_in_x": 4,
    "outer_in_y": 8,
    "arr_size": 24,
    "arr_c": 0,
    "arr_v": 4,
    "arr_d": 16,
    "union_size": 8,
    "enum_size": 4,
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
    prefix = "_" if system == "Darwin" else ""
    return dict(LP64_ABI), prefix


ABI, SYM_PREFIX = platform_abi()


_TOKEN = re.compile(r"\{([a-z_]+)\}")


def render(text):
    """Substitute only known ABI tokens; C braces are left untouched."""
    return _TOKEN.sub(lambda m: str(ABI[m.group(1)])
                      if m.group(1) in ABI else m.group(0), text)


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

STDIO = "#include <stdio.h>\n"
STDDEF = "#include <stddef.h>\n"
STDINT = "#include <stdint.h>\n"
STRING = "#include <string.h>\n"

POINTER_SIZE = STDIO + STDINT + r"""
typedef int (*fnptr)(int);
int abi_probe_fn(int x){ return x; }
int main(void){
  fnptr f = abi_probe_fn;
  int *ip = 0;
  void *vp = 0;
  printf("ptr=%zu fnptr=%zu iptr=%zu uintptr=%zu int=%zu long=%zu llong=%zu\n",
         sizeof(void*), sizeof(fnptr), sizeof(int*), sizeof(uintptr_t),
         sizeof(int), sizeof(long), sizeof(long long));
  printf("null=%d\n", (f == abi_probe_fn) && (ip == 0) && (vp == 0));
  return 0;
}
"""

SCALAR_LAYOUT = STDIO + r"""
union Uni { long l; double d; char c[8]; };
enum En { EN_ZERO = 0, EN_SEVEN = 7, EN_NINE = 9 };
int main(void){
  printf("c=%zu s=%zu i=%zu l=%zu ll=%zu f=%zu d=%zu\n",
         sizeof(char), sizeof(short), sizeof(int), sizeof(long),
         sizeof(long long), sizeof(float), sizeof(double));
  printf("union=%zu enum=%zu\n",
         sizeof(union Uni), sizeof(enum En));
  return 0;
}
"""

STRUCT_LAYOUT = STDIO + STDDEF + r"""
struct Mixed { char a; int b; double c; short d; };
int main(void){
  printf("size=%zu a=%zu b=%zu c=%zu d=%zu\n",
         sizeof(struct Mixed), offsetof(struct Mixed, a),
         offsetof(struct Mixed, b), offsetof(struct Mixed, c),
         offsetof(struct Mixed, d));
  return 0;
}
"""

STRUCT_PACKED = STDIO + STDDEF + r"""
#pragma pack(push, 1)
struct Packed { char a; int b; double c; };
#pragma pack(pop)
struct Inner { char x; int y; };
struct Outer { char a; struct Inner in; short b; };
struct Arr { char c; int v[3]; double d; };
int main(void){
  printf("packed=%zu,%zu,%zu,%zu\n",
         sizeof(struct Packed), offsetof(struct Packed, a),
         offsetof(struct Packed, b), offsetof(struct Packed, c));
  printf("outer=%zu,%zu,%zu,%zu,%zu,%zu\n",
         sizeof(struct Outer), offsetof(struct Outer, a),
         offsetof(struct Outer, in), offsetof(struct Outer, b),
         offsetof(struct Outer, in.x), offsetof(struct Outer, in.y));
  printf("arr=%zu,%zu,%zu,%zu\n",
         sizeof(struct Arr), offsetof(struct Arr, c),
         offsetof(struct Arr, v), offsetof(struct Arr, d));
  return 0;
}
"""

SIGNATURES_SCALAR = STDIO + r"""
static int add_i(int a,int b){ int t=a+b; return t; }
static long add_l(long a,long b,long c){ long t=a+b; long u=t+c; return u; }
static double mul_d(double a,double b){ double t=a*b; return t; }
static int pass_ptr(const int *p){ return *p; }
static void set_ptr(int *p,int v){ *p=v; }
int main(void){
  int slot=5; set_ptr(&slot,5);
  printf("i=%d l=%ld d=%.1f p=%d set=%d\n",
         add_i(20,22), add_l(1,2,3), mul_d(1.5,4.0),
         pass_ptr(&slot), slot);
  return 0;
}
"""

SIGNATURES_STRUCT = STDIO + r"""
struct Pair { long x; double y; };
struct Big { long a,b,c,d; };
static struct Pair mk_pair(long x,double y){ struct Pair p; p.x=x; p.y=y; return p; }
static struct Big mk_big(long a,long b,long c,long d){
  struct Big s; s.a=a; s.b=b; s.c=c; s.d=d; return s;
}
static long sum_big(struct Big s){ long t=s.a+s.b; long u=s.c+s.d; long v=t+u; return v; }
int main(void){
  struct Pair p=mk_pair(7,2.5);
  struct Big g=mk_big(11,22,33,44);
  printf("p=%ld,%.1f big=%ld,%ld,%ld,%ld sz=%zu sum=%ld\n",
         p.x, p.y, g.a, g.b, g.c, g.d, sizeof(struct Big), sum_big(g));
  return 0;
}
"""

CALLBACKS = STDIO + "#include <stdlib.h>\n" + r"""
struct Pair { long x; double y; };
static int cb_add(int a,int b){ int t=a+b; return t; }
static double cb_mul(double a,double b){ double t=a*b; return t; }
static struct Pair cb_pair(long x,double y){ struct Pair p; p.x=x; p.y=y; return p; }
static int cmp_desc(const void *a,const void *b){
  int x=*(const int*)a; int y=*(const int*)b;
  if(x>y)return -1; if(x<y)return 1; return 0;
}
static int invoke2(int (*f)(int,int),int a,int b){ int t=f(a,b); return t; }
static double invoke_dbl(double (*f)(double,double),double a,double b){
  double t=f(a,b); return t;
}
static struct Pair invoke_pair(struct Pair (*f)(long,double),long a,double b){
  struct Pair p=f(a,b); return p;
}
int main(void){
  struct Pair p=invoke_pair(cb_pair,7,2.5);
  int arr[6]; arr[0]=3;arr[1]=1;arr[2]=5;arr[3]=2;arr[4]=6;arr[5]=4;
  qsort(arr,6,sizeof(int),cmp_desc);
  printf("call2=%d calld=%.1f pair=%ld,%.1f qsort=%d%d%d%d%d%d\n",
         invoke2(cb_add,20,22), invoke_dbl(cb_mul,1.5,4.0), p.x, p.y,
         arr[0],arr[1],arr[2],arr[3],arr[4],arr[5]);
  return 0;
}
"""

FNPTR_REPR = STDIO + STDINT + STRING + r"""
typedef int (*F2)(int,int);
typedef long (*F1)(long);
struct Holder { F2 f; long tag; };
static int target(int a,int b){ int t=a*10; int u=t+b; return u; }
static int other(int a,int b){ int t=a-b; return t; }
static F2 roundtrip(F2 f){ struct Holder h; h.f=f; h.tag=9; return h.f; }
int main(void){
  F2 a=target; F2 b=&target; F2 c=other; F1 g=(F1)target;
  uintptr_t ua=0; uintptr_t ub=0; uintptr_t uc=0; uintptr_t ug=0;
  memcpy(&ua,&a,sizeof a); memcpy(&ub,&b,sizeof b);
  memcpy(&uc,&c,sizeof c); memcpy(&ug,&g,sizeof g);
  printf("sz=%zu ptr=%zu ua=%zu eq=%d same=%d diff=%d cross=%d call=%d rt=%d\n",
         sizeof(F2), sizeof(void*), sizeof(uintptr_t), a==b?1:0,
         ua==ub?1:0, ua!=uc?1:0, ua==ug?1:0, a(4,2), roundtrip(a)(5,3));
  return 0;
}
"""

SYMBOLS = STDIO + r"""
int abi_glob_counter = 41;
int abi_add_exported(int a,int b){ int t=a+b; return t; }
static int abi_add_hidden(int a,int b){ int t=a-b; return t; }
int main(void){
  abi_glob_counter = 42;
  printf("sym=%d,%d,%d\n",
         abi_add_exported(3,4), abi_add_hidden(9,2), abi_glob_counter);
  return 0;
}
"""

LIB_HELPER = r"""
int abi_lib_mul(int a,int b){ int t=a*b; return t; }
double abi_lib_scale(double x){ double t=x*2.0; return t; }
struct LibPair { long a; long b; };
struct LibPair abi_lib_pair(long a,long b){
  struct LibPair p; p.a=a; p.b=b; return p;
}
"""

LIB_MAIN = STDIO + r"""
int abi_lib_mul(int,int);
double abi_lib_scale(double);
struct LibPair { long a; long b; };
struct LibPair abi_lib_pair(long,long);
int main(void){
  struct LibPair p=abi_lib_pair(6,7);
  printf("lib=%d,%.1f,%ld,%ld\n", abi_lib_mul(6,7), abi_lib_scale(1.25), p.a, p.b);
  return 0;
}
"""

SHIPPED_RUNTIME = STDIO + STDDEF + r"""
void cplus_check_bounds(const void *base, size_t count, size_t index,
                        const char *file, int line, const char *expr);
int main(void){
  int a[4]; a[0]=1;a[1]=2;a[2]=3;a[3]=4;
  cplus_check_bounds(a,4,2,"abi_probe.c",__LINE__,"a[i]");
  printf("rlib=%d\n", a[2]);
  return 0;
}
"""


@dataclasses.dataclass(frozen=True)
class Case:
    name: str
    family: str
    kind: str  # "run" | "symbols" | "lib"
    source: str
    stdout: str = ""
    object_symbols: tuple = ()          # exact set of object globals (logical)
    binary_own_symbols: tuple = ()      # exact set of abi_* globals in binary
    forbidden_symbols: tuple = ()       # must not appear as defined globals
    undefined_symbols: tuple = ()       # must appear in nm -u (logical)
    helper_symbols: tuple = ()          # exact set of helper archive globals
    helper_source: str = ""
    note: str = ""


CASES: list[Case] = [
    Case("pointer_size", "pointer_size", "run", POINTER_SIZE,
         "ptr={ptr} fnptr={fp} iptr={ptr} uintptr={uintptr} "
         "int={int} long={long} llong={llong}\nnull=1\n",
         note="sizeof(void*), function/data pointers and fundamental widths"),
    Case("scalar_layout", "pointer_size", "run", SCALAR_LAYOUT,
         "c={char} s={short} i={int} l={long} ll={llong} f={float} d={double}\n"
         "union={union_size} enum={enum_size}\n"),
    Case("struct_layout", "struct_layout", "run", STRUCT_LAYOUT,
         "size={mixed_size} a={mixed_a} b={mixed_b} c={mixed_c} d={mixed_d}\n",
         note="sizeof and offsetof for a representative mixed struct"),
    Case("struct_packed_nested", "struct_layout", "run", STRUCT_PACKED,
         "packed={packed_size},{packed_a},{packed_b},{packed_c}\n"
         "outer={outer_size},{outer_a},{outer_in},{outer_b},{outer_in_x},{outer_in_y}\n"
         "arr={arr_size},{arr_c},{arr_v},{arr_d}\n",
         note="packed, nested and array-containing struct layouts"),
    Case("signatures_scalar", "signature", "run", SIGNATURES_SCALAR,
         "i=42 l=6 d=6.0 p=5 set=5\n",
         note="int/long/double/pointer parameter and return passing"),
    Case("signatures_struct", "signature", "run", SIGNATURES_STRUCT,
         "p=7,2.5 big=11,22,33,44 sz={big_size} sum=110\n",
         note="small and large struct passed and returned by value"),
    Case("callback_conventions", "calling_convention", "run", CALLBACKS,
         "call2=42 calld=6.0 pair=7,2.5 qsort=654321\n",
         note="typed callbacks plus a qsort comparison callback"),
    Case("fnptr_representation", "function_pointer", "run", FNPTR_REPR,
         "sz={fp} ptr={ptr} ua={uintptr} eq=1 same=1 diff=1 cross=1 "
         "call=42 rt=53\n",
         note="function pointer equality and byte representation via uintptr_t"),
    Case("symbol_names", "symbol_names", "symbols", SYMBOLS,
         "sym=7,7,42\n",
         object_symbols=("abi_add_exported", "abi_glob_counter", "main"),
         binary_own_symbols=("abi_add_exported", "abi_glob_counter"),
         forbidden_symbols=("abi_add_hidden",),
         undefined_symbols=("printf",),
         note="exact C symbol names on generated object and executable"),
    Case("lib_interop_archive", "library_interop", "lib", LIB_MAIN,
         "lib=42,2.5,6,7\n",
         helper_source=LIB_HELPER,
         helper_symbols=("abi_lib_mul", "abi_lib_pair", "abi_lib_scale"),
         note="separately compiled static archive linked into a C+ program"),
    Case("lib_interop_shipped_runtime", "library_interop", "run", SHIPPED_RUNTIME,
         "rlib=3\n",
         note="call into the compiler's precompiled libcplus_runtime.a"),
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


def nm_names(path, defined_only=True, undefined_only=False):
    argv = [NM]
    if defined_only:
        argv.append("-g")
        argv.append("--defined-only")
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
            # `nm -u` prints one bare symbol name per line.
            names.append(text.split()[-1])
            continue
        # Defined symbols: `<address> <type> <name>`.  Archive member headers
        # such as `lib.a[helper.o]:` have no address and are ignored.
        parts = text.split()
        if len(parts) >= 3 and re.fullmatch(r"[0-9a-fA-F]+", parts[0]):
            names.append(parts[-1])
    return res, names


def strip_prefix(sym):
    if SYM_PREFIX and sym.startswith(SYM_PREFIX):
        return sym[len(SYM_PREFIX):]
    return sym


def build(src, mode, out, extra=()):
    argv = [BIN, "build", str(src), mode, SANITIZE]
    argv.extend(str(x) for x in extra)
    argv.extend(["-o", str(out)])
    return run_cmd(argv)


def build_object(src, mode, out, extra=()):
    argv = [BIN, "build", str(src), mode, SANITIZE]
    argv.extend(str(x) for x in extra)
    argv.extend(["-c", "-o", str(out)])
    return run_cmd(argv)


# ---------------------------------------------------------------------------
# Check evaluation
# ---------------------------------------------------------------------------


def check_run(case, mode, tag, source_path):
    binary = CASES_DIR / ("%s.%s.bin" % (case.name, tag))
    if binary.exists():
        binary.unlink()
    res = {"build": None, "run": None, "symbols": None, "lib": None,
           "reasons": []}

    b = build(source_path, mode, binary)
    res["build"] = b
    write_log("%s.%s" % (case.name, tag), "build.txt", b)
    if b["timed_out"]:
        res["reasons"].append("build TIMEOUT after %ss" % TIMEOUT)
    elif b["rc"] != 0:
        res["reasons"].append("build failed (rc=%s)" % b["rc"])
    if b["rc"] != 0 or binary.exists() is False:
        res["reasons"].append("no executable produced at %s" % binary.name)
        return res

    x = run_cmd([str(binary)], timeout=RUN_TIMEOUT)
    res["run"] = x
    write_log("%s.%s" % (case.name, tag), "run.txt", x)
    if x["timed_out"]:
        res["reasons"].append("execution TIMEOUT after %ss" % RUN_TIMEOUT)
    if x["rc"] != 0:
        res["reasons"].append("execution rc=%s, expected 0" % x["rc"])
    if x["stdout"] != render(case.stdout):
        res["reasons"].append("stdout mismatch: expected %r, got %r"
                              % (render(case.stdout), x["stdout"]))
    return res


def check_symbols(case, mode, tag, source_path):
    res = check_run(case, mode, tag, source_path)
    info = {"object": None, "binary": None, "undefined": None}
    if NM is None:
        res["reasons"].append("SKIP: nm not available on PATH")
        res["symbols"] = info
        return res

    obj = CASES_DIR / ("%s.%s.o" % (case.name, tag))
    if obj.exists():
        obj.unlink()
    ob = build_object(source_path, mode, obj)
    info["object"] = {"result": ob}
    write_log("%s.%s" % (case.name, tag), "obj_build.txt", ob)
    if ob["timed_out"] or ob["rc"] != 0 or not obj.exists():
        res["reasons"].append("object build failed (rc=%s, timed_out=%s, "
                              "exists=%s)" % (ob["rc"], ob["timed_out"],
                                              obj.exists()))
        res["symbols"] = info
        return res

    nr, names = nm_names(obj)
    raw = sorted(names)
    info["object"]["nm"] = {"result": nr, "raw": raw,
                            "logical": sorted(strip_prefix(n) for n in names)}
    write_log("%s.%s" % (case.name, tag), "obj_nm.txt", nr)
    if nr["rc"] != 0 or nr["timed_out"]:
        res["reasons"].append("nm -g on object failed (rc=%s)" % nr["rc"])
    else:
        logical = set(info["object"]["nm"]["logical"])
        expected = set(case.object_symbols)
        if logical != expected:
            res["reasons"].append("object globals: expected exact set %s, got %s"
                                  % (sorted(expected), sorted(logical)))
        for n in case.forbidden_symbols:
            if n in logical:
                res["reasons"].append("forbidden local symbol %s is global"
                                      % (SYM_PREFIX + n))

    binary = CASES_DIR / ("%s.%s.bin" % (case.name, tag))
    if binary.exists():
        br, bnames = nm_names(binary)
        own = sorted(n for n in bnames if strip_prefix(n).startswith("abi_"))
        info["binary"] = {"result": br, "raw_own": own,
                          "logical_own": sorted(strip_prefix(n) for n in own)}
        write_log("%s.%s" % (case.name, tag), "bin_nm.txt", br)
        if br["rc"] != 0 or br["timed_out"]:
            res["reasons"].append("nm -g on executable failed (rc=%s)" % br["rc"])
        else:
            expected = {SYM_PREFIX + n for n in case.binary_own_symbols}
            if set(own) != expected:
                res["reasons"].append("executable abi_* globals: expected %s, got %s"
                                      % (sorted(expected), own))
            for n in case.forbidden_symbols:
                if SYM_PREFIX + n in own:
                    res["reasons"].append("forbidden local symbol %s exported"
                                          % (SYM_PREFIX + n))
            if SYM_PREFIX + "main" not in bnames:
                res["reasons"].append("main symbol missing from executable")
        ur, unames = nm_names(binary, defined_only=False, undefined_only=True)
        info["undefined"] = {"result": ur, "raw": sorted(set(unames))}
        write_log("%s.%s" % (case.name, tag), "undef_nm.txt", ur)
        if ur["rc"] != 0 or ur["timed_out"]:
            res["reasons"].append("nm -u on executable failed (rc=%s)" % ur["rc"])
        else:
            for n in case.undefined_symbols:
                if SYM_PREFIX + n not in unames:
                    res["reasons"].append("undefined symbol %s not found"
                                          % (SYM_PREFIX + n))
    res["symbols"] = info
    return res


def check_lib(case, mode, tag, source_path):
    # Build the helper archive first, then build/run the main program.
    res = {"build": None, "run": None, "symbols": None, "lib": None,
           "reasons": []}
    if AR is None:
        res["reasons"].append("SKIP: ar not available on PATH")
        res["lib"] = {"archive": None}
        return res

    helper_src = CASES_DIR / ("%s.%s.helper.c" % (case.name, tag))
    helper_src.write_text(case.helper_source.lstrip("\n"))
    helper_obj = CASES_DIR / ("%s.%s.helper.o" % (case.name, tag))
    lib_name = "abi_lib_helper_%s" % tag
    archive = CASES_DIR / ("lib%s.a" % lib_name)
    for p in (helper_obj, archive):
        if p.exists():
            p.unlink()

    hb = build_object(helper_src, mode, helper_obj)
    write_log("%s.%s" % (case.name, tag), "helper_build.txt", hb)
    ab = None
    if hb["timed_out"] or hb["rc"] != 0 or not helper_obj.exists():
        res["reasons"].append("helper object build failed (rc=%s)" % hb["rc"])
    else:
        ab = run_cmd([AR, "rcs", str(archive), str(helper_obj)], timeout=30)
        write_log("%s.%s" % (case.name, tag), "ar.txt", ab)
        if ab["timed_out"] or ab["rc"] != 0 or not archive.exists():
            res["reasons"].append("archive creation failed (rc=%s)" % ab["rc"])

    lib_info = {"helper_build": hb, "ar": ab, "helper_nm": None}
    if ab is not None and ab["rc"] == 0 and archive.exists() and NM is not None:
        nr, names = nm_names(archive)
        logical = sorted(strip_prefix(n) for n in names)
        lib_info["helper_nm"] = {"result": nr, "raw": sorted(names),
                                 "logical": logical}
        write_log("%s.%s" % (case.name, tag), "helper_nm.txt", nr)
        if nr["rc"] != 0 or nr["timed_out"]:
            res["reasons"].append("nm -g on archive failed (rc=%s)" % nr["rc"])
        elif set(logical) != set(case.helper_symbols):
            res["reasons"].append("archive globals: expected %s, got %s"
                                  % (sorted(case.helper_symbols), logical))
    res["lib"] = lib_info
    if res["reasons"]:
        return res

    # Reuse the ordinary run path, adding -L/-l so the archive participates.
    binary = CASES_DIR / ("%s.%s.bin" % (case.name, tag))
    if binary.exists():
        binary.unlink()
    b = build(source_path, mode, binary,
              extra=("-L" + str(CASES_DIR), "-l" + lib_name))
    res["build"] = b
    write_log("%s.%s" % (case.name, tag), "build.txt", b)
    if b["timed_out"]:
        res["reasons"].append("build TIMEOUT after %ss" % TIMEOUT)
    elif b["rc"] != 0:
        res["reasons"].append("build failed (rc=%s)" % b["rc"])
    if b["rc"] != 0 or not binary.exists():
        res["reasons"].append("no executable produced at %s" % binary.name)
        return res

    x = run_cmd([str(binary)], timeout=RUN_TIMEOUT)
    res["run"] = x
    write_log("%s.%s" % (case.name, tag), "run.txt", x)
    if x["timed_out"]:
        res["reasons"].append("execution TIMEOUT after %ss" % RUN_TIMEOUT)
    if x["rc"] != 0:
        res["reasons"].append("execution rc=%s, expected 0" % x["rc"])
    if x["stdout"] != render(case.stdout):
        res["reasons"].append("stdout mismatch: expected %r, got %r"
                              % (render(case.stdout), x["stdout"]))
    return res


def evaluate(case, mode):
    tag = mode_tag(mode)
    source_path = CASES_DIR / ("%s.%s.c" % (case.name, tag))
    source_path.write_text(render(case.source).lstrip("\n"))
    if case.kind == "run":
        res = check_run(case, mode, tag, source_path)
    elif case.kind == "symbols":
        res = check_symbols(case, mode, tag, source_path)
    elif case.kind == "lib":
        res = check_lib(case, mode, tag, source_path)
    else:
        raise AssertionError("unknown case kind %s" % case.kind)

    skip = any(r.startswith("SKIP:") for r in res["reasons"])
    return {
        "case": case.name,
        "family": case.family,
        "kind": case.kind,
        "mode": mode,
        "status": "skip" if skip else ("pass" if not res["reasons"] else "fail"),
        "note": case.note,
        "expected_stdout": render(case.stdout),
        "source": source_path.read_text(),
        "build": res["build"],
        "run": res["run"],
        "symbols": res["symbols"],
        "lib": res["lib"],
        "reasons": res["reasons"],
    }


# ---------------------------------------------------------------------------
# Summary / reporting
# ---------------------------------------------------------------------------


def version_string():
    res = run_cmd([BIN, "--version"], timeout=15)
    return (res["stdout"] + res["stderr"]).strip().splitlines()[:1]


def write_summary(records):
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

    payload = {
        "suite": "abi_safety",
        "generated_at": datetime.datetime.now().isoformat(timespec="seconds"),
        "binary": BIN,
        "binary_version": version_string(),
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
        "tools": {"nm": NM, "ar": AR},
        "totals": {
            "cases": len(CASES),
            "checks": len(records),
            "passed": statuses["pass"],
            "failed": statuses["fail"],
            "skipped": statuses["skip"],
        },
        "by_family": by_family,
        "by_mode": by_mode,
        "failures": [
            {"case": r["case"], "family": r["family"], "mode": r["mode"],
             "reasons": r["reasons"],
             "expected_stdout": r["expected_stdout"],
             "actual_stdout": (r["run"] or {}).get("stdout")}
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
    add("C+ native ABI regression suite")
    add("=" * 68)
    add("binary       : %s" % BIN)
    add("version      : %s" % ((payload["binary_version"] or ["(unknown)"])[0]))
    add("platform     : %s/%s (symbol prefix %r)"
        % (platform_mod.system(), platform_mod.machine(), SYM_PREFIX))
    add("generated    : %s" % payload["generated_at"])
    add("modes        : %s" % ", ".join(MODES))
    add("backend flags: %s (no sanitizer instrumentation; ABI only)" % SANITIZE)
    add("timeout      : %ss build/nm, %ss execution" % (TIMEOUT, RUN_TIMEOUT))
    t = payload["totals"]
    add("")
    add("SUMMARY")
    add("-" * 68)
    add("cases                : %d" % t["cases"])
    add("checks (case x mode) : %d" % t["checks"])
    add("passed               : %d" % t["passed"])
    add("failed               : %d" % t["failed"])
    add("skipped              : %d" % t["skipped"])
    add("")
    add("BY MODE")
    add("-" * 68)
    for mode, info in by_mode.items():
        add("  %-10s checks=%2d passed=%2d failed=%2d skipped=%2d"
            % (mode, info["checks"], info["pass"], info["fail"], info["skip"]))
    add("")
    add("BY FAMILY")
    add("-" * 68)
    for fam, info in by_family.items():
        add("  %-20s checks=%2d passed=%2d failed=%2d skipped=%2d"
            % (fam, info["checks"], info["pass"], info["fail"], info["skip"]))
    add("")
    add("FAILURES (exact case names and modes)")
    add("-" * 68)
    if not failures:
        add("  none")
    else:
        for f in failures:
            add("  %-28s mode=%-10s family=%s"
                % (f["case"], f["mode"], f["family"]))
            for reason in f["reasons"]:
                add("      - %s" % reason)
    add("")
    add("SKIPS")
    add("-" * 68)
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

    print("abi_safety: %d cases x %d modes, binary=%s"
          % (len(CASES), len(MODES), BIN))
    print("platform: %s/%s, nm=%s, ar=%s"
          % (platform_mod.system(), platform_mod.machine(),
             NM or "UNAVAILABLE", AR or "UNAVAILABLE"))

    work = [(case, mode) for case in CASES for mode in MODES]
    records = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        futures = [pool.submit(evaluate, case, mode) for case, mode in work]
        for future in concurrent.futures.as_completed(futures):
            records.append(future.result())
    records.sort(key=lambda r: (r["case"], MODES.index(r["mode"])))

    payload = write_summary(records)
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
    return 1 if payload["failures"] else 0


if __name__ == "__main__":
    sys.exit(main())
