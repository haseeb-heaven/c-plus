#!/usr/bin/env python3
"""Native regression suite for C+ interprocedural safety.

The suite exercises interprocedural bounds / ownership / effect reasoning by
compiling small, self-contained C programs with the *native* compiler binary
(``$CPLUS_BIN``, default ``$PWD/build/c+``) in three modes:

  * --default  : report proven errors, warn on unverified operations
  * --strict   : reject unverified operations (fail closed)
  * --legacy-c : compatibility mode; deliberately NOT memory safe

Every case declares, per mode, the expected compile result (accept / reject)
and an *exact substring* that must appear in the normalised compiler output.
Accepted, safe cases are additionally built and executed; their expected
stdout substring and exit status are recorded too.

Normalisation replaces the absolute fixture path with ``<SRC>`` so expected
substrings are stable across machines.  There is no ``|| true`` anywhere:
every subprocess return code is classified explicitly, and timeouts are hard
failures.  All subprocesses run under a per-call timeout (IPS_TIMEOUT).

Results are written next to this script under ``results/``:

  results/summary.txt    human-readable counts + failure names
  results/summary.json   machine-readable full record (incl. all diagnostics)
  results/logs/<case>.<mode>.txt   full normalised output for one check
  results/logs/<case>.build.txt    build output for runtime cases
  results/logs/<case>.run.txt      execution output for runtime cases
  cases/<case>.c          generated fixture (rewritten on every run)

Exit status is 0 only when every case matches every expectation.
"""

from __future__ import annotations

import concurrent.futures
import dataclasses
import datetime
import json
import os
import pathlib
import subprocess
import sys
from collections import Counter
from typing import Optional

HERE = pathlib.Path(__file__).resolve().parent
CASES_DIR = HERE / "cases"
RESULTS = HERE / "results"
LOGS = RESULTS / "logs"

BIN = os.environ.get("CPLUS_BIN", str(pathlib.Path.cwd() / "build" / "c+"))
MODES = ("--default", "--strict", "--legacy-c")
MODE_ATTRS = {"--default": "default", "--strict": "strict", "--legacy-c": "legacy"}
TIMEOUT = int(os.environ.get("IPS_TIMEOUT", "90"))
WORKERS = int(os.environ.get("IPS_WORKERS", "8"))

OK = "check: <SRC> ok"
BOUNDS = "[CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS]"
BOUNDS_ONLY = "[CPLUS-BOUNDS]"
LIFETIME = "[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME]"
ARITH = "[CPLUS-ARITH]"
FFI = "[CPLUS-FFI]"
UNVERIFIABLE = "pointer parameter 'p' has unverifiable extent; subscript cannot be proven safe"

STD = "#include <stdlib.h>\n"
STDIO = "#include <stdio.h>\n"


@dataclasses.dataclass(frozen=True)
class Expect:
    reject: bool
    substring: Optional[object] = None  # str or tuple of exact substrings


def substrings_of(expect: Expect):
    if expect.substring is None:
        return []
    if isinstance(expect.substring, str):
        return [expect.substring]
    return list(expect.substring)


@dataclasses.dataclass(frozen=True)
class RuntimeExpect:
    stdout: str
    exit_code: int = 0


@dataclasses.dataclass(frozen=True)
class Case:
    name: str
    family: str
    polarity: str
    source: str
    default: Expect
    strict: Expect
    legacy: Expect
    runtime: Optional[RuntimeExpect] = None
    note: str = ""


def safe(name, family, source, stdout, note=""):
    """Well-formed program: accepted in all modes, executed at runtime."""
    ok = Expect(False, OK)
    return Case(name, family, "positive", source, ok, ok, ok,
                RuntimeExpect(stdout), note)


def unsafe(name, family, source, substring, note="", gap=False):
    """Proven error: rejected in DEFAULT+STRICT, accepted in LEGACY."""
    return Case(name, family, "soundness-gap" if gap else "negative", source,
                Expect(True, substring), Expect(True, substring),
                Expect(False, OK), None, note)


def strict_only(name, family, source, substring, note="",
                default_substring=None):
    """Unverified operation: DEFAULT warns/accepts, STRICT rejects."""
    if default_substring is None:
        default_substring = substring
    return Case(name, family, "strict-only", source,
                Expect(False, (OK, default_substring)), Expect(True, substring),
                Expect(False, OK), None, note)


def both_reject(name, family, source, substring, note=""):
    """Unconditional rejection (incl. DEFAULT warnings promoted to errors)."""
    return Case(name, family, "negative", source,
                Expect(True, substring), Expect(True, substring),
                Expect(False, OK), None, note)


CASES: list[Case] = [
    # ------------------------------------------------------------------
    # Direct / nested / recursive / mutually recursive calls
    # ------------------------------------------------------------------
    safe("call_direct_ok", "call_shape", STD + STDIO + """
static void wr(int *p){ p[3]=9; }
int main(void){ int *a=malloc(4*sizeof(int)); if(!a)return 2; wr(a);
  printf("v=%d\\n",a[3]); free(a); return 0; }
""", "v=9"),

    unsafe("call_direct_oob_write", "call_shape", STD + """
static void wr(int *p){ p[6]=9; }
int main(void){ int *a=malloc(4*sizeof(int)); if(!a)return 2; wr(a); free(a); return 0; }
""", BOUNDS + " array index 6 exceeds heap allocation 'p' of 16 bytes"),

    unsafe("call_direct_oob_read", "call_shape", STDIO + """
static int rd(int *p){ return p[9]; }
int main(void){ int a[4]={1,2,3,4}; printf("v=%d\\n",rd(a)); return 0; }
""", BOUNDS + " array index 9 exceeds stack array 'p' of 16 bytes"),

    safe("call_nested_ok", "call_shape", STDIO + """
static void g(int *p){ p[2]=8; }
static void f(int *p){ g(p); }
int main(void){ int a[4]={0}; f(a); printf("v=%d\\n",a[2]); return 0; }
""", "v=8"),

    unsafe("call_nested_oob", "call_shape", """
static void g(int *p){ p[5]=1; }
static void f(int *p){ g(p); }
int main(void){ int a[4]={0}; f(a); return a[0]; }
""", BOUNDS + " array index 5 exceeds stack array 'p' of 16 bytes"),

    safe("call_nested_deep_ok", "call_shape", STDIO + """
static void h(int *p){ p[3]=6; }
static void g(int *p){ h(p); }
static void f(int *p){ g(p); }
int main(void){ int a[4]={0}; f(a); printf("v=%d\\n",a[3]); return 0; }
""", "v=6"),

    unsafe("call_nested_deep_oob", "call_shape", """
static void h(int *p){ p[6]=1; }
static void g(int *p){ h(p); }
static void f(int *p){ g(p); }
int main(void){ int a[4]={0}; f(a); return a[0]; }
""", BOUNDS + " array index 6 exceeds stack array 'p' of 16 bytes"),

    safe("call_recursive_ok", "call_shape", STDIO + """
static int rec(int *p,int i){ if(i>=3) return p[i]; return rec(p,i+1); }
int main(void){ int a[4]={1,2,3,4}; printf("v=%d\\n",rec(a,0)); return 0; }
""", "v=4"),

    unsafe("call_recursive_oob", "call_shape", """
static void rec(int *p,int i){ if(i>5) return; p[i]=1; rec(p,i+1); }
int main(void){ int a[4]={0}; rec(a,0); return a[0]; }
""", BOUNDS,
           note="soundness probe: recursion walks past a 4-element buffer; "
                "currently accepted because the index is runtime-valued",
           gap=True),

    safe("call_mutual_ok", "call_shape", STDIO + """
static int g(int*,int);
static int f(int *p,int i){ if(i>=3) return p[i]; return g(p,i+1); }
static int g(int *p,int i){ return f(p,i); }
int main(void){ int a[4]={1,2,3,4}; printf("v=%d\\n",f(a,0)); return 0; }
""", "v=4"),

    unsafe("call_mutual_oob", "call_shape", """
static void g(int *p,int i);
static void f(int *p,int i){ if(i>5)return; p[i]=1; g(p,i+1); }
static void g(int *p,int i){ f(p,i); }
int main(void){ int a[4]={0}; f(a,0); return a[0]; }
""", BOUNDS,
           note="soundness probe: mutual recursion walks past the buffer; "
                "currently accepted", gap=True),

    # ------------------------------------------------------------------
    # Array decay to a pointer parameter
    # ------------------------------------------------------------------
    safe("decay_stack_ok", "array_decay", STDIO + """
int first(int *p){ return p[3]; }
int main(void){ int a[4]={1,2,3,5}; printf("v=%d\\n",first(a)); return 0; }
""", "v=5"),

    unsafe("decay_stack_oob", "array_decay", """
int first(int *p){ return p[4]; }
int main(void){ int a[4]; a[0]=1; return first(a); }
""", BOUNDS + " array index 4 exceeds stack array 'p' of 16 bytes",
           note="the canonical array-decay regression: int first(int *p) with int a[4]"),

    safe("decay_heap_ok", "array_decay", STD + STDIO + """
static int first(int *p){ return p[3]; }
int main(void){ int *a=malloc(16); if(!a)return 2; a[3]=6;
  printf("v=%d\\n",first(a)); free(a); return 0; }
""", "v=6"),

    unsafe("decay_heap_oob", "array_decay", STD + """
static int first(int *p){ return p[4]; }
int main(void){ int *a=malloc(16); if(!a)return 2; int v=first(a); (void)v; free(a); return 0; }
""", BOUNDS + " array index 4 exceeds heap allocation 'p' of 16 bytes"),

    # ------------------------------------------------------------------
    # Pointer parameter bounds / conflicting extents
    # ------------------------------------------------------------------
    safe("param_bounds_size_ok", "param_bounds", STDIO + """
static void fill(int *p,int n){ for(int i=0;i<n;i++) p[i]=i; }
int main(void){ int a[4]={0}; fill(a,4); printf("v=%d\\n",a[3]); return 0; }
""", "v=3"),

    unsafe("param_bounds_oob", "param_bounds", STD + """
static void put(int *p){ p[10]=1; }
int main(void){ int *a=malloc(8); if(!a)return 2; put(a); free(a); return 0; }
""", BOUNDS + " array index 10 exceeds heap allocation 'p' of 8 bytes"),

    strict_only("param_bounds_conflict", "param_bounds", """
static void wr(int *p){ p[2]=1; }
int main(void){ int a[4]={0}; int b[8]={0}; wr(a); wr(b); return a[2]; }
""", FFI + " " + UNVERIFIABLE,
                 note="two call sites pass different extents for the same parameter"),

    strict_only("param_bounds_untracked", "param_bounds", STD + """
static void wr(int *p){ p[2]=1; }
int main(int argc,char **argv){ (void)argv; int a[4]={0}; int *q = argc ? a : 0;
  wr(q); return a[0]; }
""", FFI + " " + UNVERIFIABLE,
                 note="conditionally initialised pointer has no proven extent"),

    # ------------------------------------------------------------------
    # Returned pointer ownership
    # ------------------------------------------------------------------
    safe("ret_borrow_ok", "ret_ownership", STD + STDIO + """
static int *id(int *p){ return p; }
int main(void){ int *a=malloc(16); if(!a)return 2; int *q=id(a);
  q[3]=5; printf("v=%d\\n",q[3]); free(a); return 0; }
""", "v=5"),

    unsafe("ret_borrow_oob", "ret_ownership", STD + """
static int *id(int *p){ return p; }
int main(void){ int *a=malloc(16); if(!a)return 2; int *q=id(a);
  q[6]=1; free(a); return 0; }
""", BOUNDS + " array index 6 exceeds heap allocation 'q' of 16 bytes"),

    unsafe("ret_freed_uaf", "ret_ownership", STD + """
static void rel(int *p){ free(p); }
int main(void){ int *a=malloc(16); if(!a)return 2; rel(a); int v=*a; (void)v; return 0; }
""", LIFETIME + " use-after-free: dereference of released pointer 'a'"),

    both_reject("ret_double_free", "ret_ownership", STD + """
static void rel(int *p){ free(p); }
int main(void){ int *a=malloc(4); if(!a)return 2; rel(a); rel(a); return 0; }
""", LIFETIME + " use-after-free: released pointer 'a' passed to 'rel'",
                 note="DEFAULT reports this as a warning that still fails the check"),

    both_reject("ret_local_uaf", "ret_ownership", """
static int *mk(void){ int x=7; return &x; }
int main(void){ int *p=mk(); int v=*p; (void)v; return 0; }
""", "address of stack memory associated with local variable 'x' returned"),

    safe("ret_owned_ok", "ret_ownership", STD + STDIO + """
static int *make(void){ int *p=malloc(16); if(!p)return 0; p[0]=4; return p; }
int main(void){ int *q=make(); if(!q)return 2; printf("v=%d\\n",q[0]); free(q); return 0; }
""", "v=4"),

    # ------------------------------------------------------------------
    # Nullability propagation
    # ------------------------------------------------------------------
    both_reject("null_return_deref", "nullability", """
static int *getnull(void){ return 0; }
int main(void){ int *p=getnull(); return *p; }
""", LIFETIME + " null pointer dereference"),

    safe("null_return_guard_ok", "nullability", STDIO + """
static int *getnull(void){ return 0; }
int main(void){ int *p=getnull(); if(p) printf("bad\\n"); else printf("v=0\\n"); return 0; }
""", "v=0"),

    strict_only("null_arg_callee", "nullability", """
static int deref(int *p){ return *p; }
int main(void){ int *q=0; return deref(q); }
""", LIFETIME + " null pointer argument 0 passed to 'deref'",
                 note="callee dereferences parameter 0; a provably-null argument "
                      "is a DEFAULT warning and a STRICT error"),

    both_reject("null_libc_arg", "nullability", "#include <string.h>\n" + """
int main(void){ char a[8]; char *q=0; memcpy(a,q,4); return a[0]; }
""", LIFETIME + " null pointer argument 1 passed to memcpy"),

    safe("null_checked_ok", "nullability", STDIO + """
static int deref(int *p){ if(!p) return -1; return *p; }
int main(void){ printf("v=%d\\n",deref(0)); return 0; }
""", "v=-1",
         note="A callee that explicitly handles null may receive null; "
              "rejecting this would reject valid C"),

    safe("null_checked_nonnull_ok", "nullability", STDIO + """
static int deref(int *p){ if(!p) return -1; return *p; }
int main(void){ int a=6; printf("v=%d\\n",deref(&a)); return 0; }
""", "v=6"),

    # ------------------------------------------------------------------
    # Ownership transfer / release effects
    # ------------------------------------------------------------------
    safe("owner_transfer_ok", "ownership", STD + STDIO + """
static void rel(int *p){ free(p); }
int main(void){ int *a=malloc(4); if(!a)return 2; *a=9; rel(a);
  printf("done\\n"); return 0; }
""", "done"),

    unsafe("owner_uaf_after_transfer", "ownership", STD + """
static void rel(int *p){ free(p); }
int main(void){ int *a=malloc(4); if(!a)return 2; rel(a); int v=*a; (void)v; return 0; }
""", LIFETIME + " use-after-free: dereference of released pointer 'a'"),

    both_reject("owner_double_release", "ownership", STD + """
static void rel(int *p){ free(p); }
int main(void){ int *a=malloc(4); if(!a)return 2; rel(a); free(a); return 0; }
""", "double free",
                 note="release inside a callee followed by a direct free in the caller"),

    # ------------------------------------------------------------------
    # Read / write effects
    # ------------------------------------------------------------------
    unsafe("effect_write_oob", "effects", """
static void wr(int *p){ p[8]=9; }
int main(void){ int a[4]={0}; wr(a); return a[0]; }
""", BOUNDS + " array index 8 exceeds stack array 'p' of 16 bytes"),

    safe("effect_read_ok", "effects", STDIO + """
static int rd(int *p){ return p[1]; }
int main(void){ int a[4]={1,2,3,4}; printf("v=%d\\n",rd(a)); return 0; }
""", "v=2"),

    # ------------------------------------------------------------------
    # Global variable / global array effects
    # ------------------------------------------------------------------
    safe("global_var_ok", "globals", STDIO + """
int g;
static void setg(void){ g=5; }
int main(void){ setg(); printf("v=%d\\n",g); return 0; }
""", "v=5"),

    both_reject("global_array_oob", "globals", """
int g[4];
static void wr(void){ g[7]=3; }
int main(void){ wr(); return g[0]; }
""", "[CPLUS-SAFE-BOUNDS] constant array index 7 is outside the bounds of 'g'"),

    safe("global_array_ok", "globals", STDIO + """
int g[4]={1,2,3,4};
static int rd(void){ return g[2]; }
int main(void){ printf("v=%d\\n",rd()); return 0; }
""", "v=3"),

    both_reject("global_pointer_oob", "globals", """
int garr[4]; int *gp=garr;
static void wr(void){ gp[7]=1; }
int main(void){ wr(); return garr[0]; }
""", BOUNDS_ONLY +
        " out-of-bounds access: index 7 reaches past array 'garr' via pointer 'gp'"),

    # ------------------------------------------------------------------
    # Aliasing / pointer-to-pointer
    # ------------------------------------------------------------------
    safe("alias_ok", "alias", STDIO + """
static int rd(int *p){ int *q=p; return q[2]; }
int main(void){ int a[4]={1,2,3,4}; printf("v=%d\\n",rd(a)); return 0; }
""", "v=3"),

    unsafe("alias_oob", "alias", """
static void wr(int *p){ int *q=p; q[6]=1; }
int main(void){ int a[4]={0}; wr(a); return a[0]; }
""", BOUNDS + " array index 6 exceeds stack array 'q' of 16 bytes"),

    safe("ptr2_ok", "alias", STDIO + """
static int rd(int **pp){ return (*pp)[2]; }
int main(void){ int a[4]={1,2,3,4}; int *p=a; printf("v=%d\\n",rd(&p)); return 0; }
""", "v=3"),

    unsafe("ptr2_oob", "alias", """
static void wr(int **pp){ (*pp)[6]=1; }
int main(void){ int a[4]={0}; int *p=a; wr(&p); return a[0]; }
""", BOUNDS,
           note="soundness probe: write through int ** exceeds the 4-element "
                "object; currently accepted", gap=True),

    # ------------------------------------------------------------------
    # Struct pointer fields / fixed array fields
    # ------------------------------------------------------------------
    safe("struct_array_ok", "structs", STDIO + """
struct B{ int a[4]; };
static int rd(struct B *b){ return b->a[2]; }
int main(void){ struct B b={{1,2,3,4}}; printf("v=%d\\n",rd(&b)); return 0; }
""", "v=3"),

    both_reject("struct_array_oob", "structs", """
struct B{ int a[4]; };
static void wr(struct B *b){ b->a[7]=1; }
int main(void){ struct B b; wr(&b); return b.a[0]; }
""", "[CPLUS-SAFE-BOUNDS] constant array index 7 is outside the bounds of 'b->a'"),

    safe("struct_ptr_ok", "structs", STDIO + """
struct B{ int *p; };
static int rd(struct B *b){ return b->p[2]; }
int main(void){ int a[4]={1,2,3,4}; struct B b; b.p=a;
  printf("v=%d\\n",rd(&b)); return 0; }
""", "v=3"),

    unsafe("struct_ptr_oob", "structs", """
struct B{ int *p; };
static void wr(struct B *b){ b->p[6]=1; }
int main(void){ int a[4]={0}; struct B b; b.p=a; wr(&b); return a[0]; }
""", BOUNDS,
           note="soundness probe: extent behind a struct pointer field is not "
                "propagated; currently accepted", gap=True),

    # ------------------------------------------------------------------
    # Pointer reassignment
    # ------------------------------------------------------------------
    both_reject("ptr_reassign_oob", "reassignment", """
static void wr(int *p){ int dummy[2]={0}; p=dummy; p[5]=1; }
int main(void){ int a[4]={0}; wr(a); return a[0]; }
""", BOUNDS_ONLY +
        " out-of-bounds access: index 5 reaches past array 'dummy' via pointer 'p'"),

    strict_only("ptr_reassign_unverified", "reassignment", """
static void wr(int *p){ int dummy[8]={0}; p=dummy; p[7]=1; }
int main(void){ int a[4]={0}; wr(a); return a[0]; }
""", FFI + " " + UNVERIFIABLE,
                 note="in-bounds after reassignment but STRICT rejects the "
                      "unverifiable parameter extent"),

    # ------------------------------------------------------------------
    # Branch / loop / dataflow
    # ------------------------------------------------------------------
    unsafe("branch_oob", "dataflow", """
static void wr(int *p,int c){ if(c) p[8]=1; else p[0]=2; }
int main(void){ int a[4]={0}; wr(a,1); return a[0]; }
""", BOUNDS + " array index 8 exceeds stack array 'p' of 16 bytes"),

    safe("branch_ok", "dataflow", STDIO + """
static void wr(int *p,int c){ if(c) p[2]=1; else p[0]=2; }
int main(void){ int a[4]={0}; wr(a,1); printf("v=%d\\n",a[2]); return 0; }
""", "v=1"),

    unsafe("loop_for_oob", "dataflow", """
static void wr(int *p){ for(int i=0;i<6;i++) p[i]=i; }
int main(void){ int a[4]={0}; wr(a); return a[0]; }
""", BOUNDS + " array index 5 exceeds stack array 'p' of 16 bytes"),

    safe("loop_for_ok", "dataflow", STDIO + """
static void wr(int *p){ for(int i=0;i<4;i++) p[i]=i; }
int main(void){ int a[4]={0}; wr(a); printf("v=%d\\n",a[3]); return 0; }
""", "v=3"),

    unsafe("loop_while_oob", "dataflow", """
static void wr(int *p){ int i=0; while(i<7){ p[i]=i; i++; } }
int main(void){ int a[4]={0}; wr(a); return a[0]; }
""", BOUNDS,
           note="soundness probe: while-loop trip count past the buffer is "
                "not propagated; currently accepted", gap=True),

    unsafe("dataflow_phi_oob", "dataflow", """
static void wr(int *p,int c){ int i=c?1:6; p[i]=1; }
int main(void){ int a[4]={0}; wr(a,0); return a[0]; }
""", BOUNDS,
           note="soundness probe: index chosen by a conditional expression is "
                "not propagated; currently accepted", gap=True),

    # ------------------------------------------------------------------
    # Static arithmetic / conversion
    # ------------------------------------------------------------------
    safe("arith_ok", "static_arith", STDIO + """
int main(void){ int x=7; int y=x*3; printf("v=%d\\n",y); return 0; }
""", "v=21"),

    strict_only("arith_overflow_strict", "static_arith", """
int main(void){ int x=2147483647; int y=x+1; return y==0; }
""", ARITH + " signed integer overflow in addition"),

    strict_only("arith_divzero_strict", "static_arith", """
int main(void){ int z=0; int x=10/z; return x; }
""", ARITH + " division by zero"),

    both_reject("arith_alloc_wrap", "static_arith", STD + """
int main(void){ int n=2147483647; int *p=malloc(n*4); if(!p)return 2;
  p[0]=1; free(p); return 0; }
""", FFI + " allocation size wraps size_t; check the multiplication before allocating"),

    both_reject("conv_shift_oob", "static_arith", """
int main(void){ int x=1; int y=x<<64; return y; }
""", FFI + " shift count 64 is not less than the width 32 of the left operand",
                 note="DEFAULT emits a warning that fails the check; STRICT emits an error"),
]


def normalise(text: str, path: pathlib.Path) -> str:
    """Make diagnostics machine-stable across checkouts."""
    text = text.replace(str(path), "<SRC>")
    text = text.replace(str(path.with_suffix("")), "<SRC>")
    return text.strip()


def run_cmd(argv, timeout=TIMEOUT):
    try:
        p = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        return {"rc": p.returncode,
                "stdout": p.stdout,
                "stderr": p.stderr,
                "timed_out": False}
    except subprocess.TimeoutExpired as exc:
        out = exc.stdout or b""
        err = exc.stderr or b""
        if isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        if isinstance(err, bytes):
            err = err.decode("utf-8", "replace")
        return {"rc": None, "stdout": out, "stderr": err, "timed_out": True}
    except OSError as exc:
        return {"rc": None, "stdout": "", "stderr": "OS ERROR: %s" % exc,
                "timed_out": False}


def check_one(case: Case, mode: str, path: pathlib.Path):
    res = run_cmd([BIN, "check", mode, str(path)])
    diag = normalise(res["stdout"] + res["stderr"], path)
    (LOGS / ("%s.%s.txt" % (case.name, mode.lstrip("-")))).write_text(
        "command: c+ check %s %s\nreturncode: %s\ntimed_out: %s\n\n%s\n"
        % (mode, path, res["rc"], res["timed_out"], diag))
    rejected = res["rc"] is not None and res["rc"] != 0
    crashed = res["rc"] is not None and res["rc"] < 0
    return {"rejected": rejected, "returncode": res["rc"],
            "timed_out": res["timed_out"], "crashed": crashed,
            "diagnostic": diag}


def runtime_one(case: Case, path: pathlib.Path):
    binary = CASES_DIR / (case.name + ".bin")
    if binary.exists():
        binary.unlink()
    build = run_cmd([BIN, "build", str(path), "--sanitize=none",
                     "-o", str(binary)])
    build_log = "command: c+ build %s --sanitize=none -o %s\nreturncode: %s\n" \
                "timed_out: %s\n\n%s\n" % (
                    path, binary, build["rc"], build["timed_out"],
                    normalise(build["stdout"] + build["stderr"], path))
    (LOGS / (case.name + ".build.txt")).write_text(build_log)
    out = {"built": False, "ran": False, "exit_code": None,
           "stdout": "", "passed": False, "reasons": []}
    if build["rc"] != 0 or not binary.exists():
        out["reasons"].append("build failed (rc=%s, timed_out=%s)"
                              % (build["rc"], build["timed_out"]))
        return out
    out["built"] = True
    run = run_cmd([str(binary)], timeout=30)
    stdout = run["stdout"]
    (LOGS / (case.name + ".run.txt")).write_text(
        "command: %s\nreturncode: %s\ntimed_out: %s\n\n%s\n"
        % (binary, run["rc"], run["timed_out"], stdout))
    out["stdout"] = stdout
    out["exit_code"] = run["rc"]
    if run["timed_out"]:
        out["reasons"].append("execution TIMEOUT after 30s")
        return out
    expected = case.runtime
    if run["rc"] != expected.exit_code:
        out["reasons"].append("exit code %s != expected %s"
                              % (run["rc"], expected.exit_code))
    if expected.stdout not in stdout:
        out["reasons"].append("missing stdout substring %r" % expected.stdout)
    out["passed"] = not out["reasons"]
    out["ran"] = True
    return out


def evaluate_case(case: Case):
    path = CASES_DIR / (case.name + ".c")
    path.write_text(case.source.lstrip("\n"))

    modes = {}
    reasons = []
    for mode in MODES:
        actual = check_one(case, mode, path)
        expected = getattr(case, MODE_ATTRS[mode])
        mode_reasons = []
        if actual["timed_out"]:
            mode_reasons.append("TIMEOUT after %ds" % TIMEOUT)
        if actual["crashed"]:
            mode_reasons.append("compiler crashed (rc=%s)" % actual["returncode"])
        if not actual["timed_out"] and actual["rejected"] != expected.reject:
            mode_reasons.append(
                "expected %s but got %s" %
                ("reject" if expected.reject else "accept",
                 "reject" if actual["rejected"] else "accept"))
        for sub in substrings_of(expected):
            if sub not in actual["diagnostic"]:
                mode_reasons.append("missing exact substring %r" % sub)
        modes[mode] = {
            "expected_reject": expected.reject,
            "expected_substring": expected.substring,
            "actual_reject": actual["rejected"],
            "actual_returncode": actual["returncode"],
            "actual_diagnostic": actual["diagnostic"],
            "passed": not mode_reasons,
            "reasons": mode_reasons,
        }
        if mode_reasons:
            reasons.append({"mode": mode, "reasons": mode_reasons})

    runtime = runtime_one(case, path) if case.runtime is not None else None
    if runtime is not None and not runtime["passed"]:
        reasons.append({"mode": "runtime", "reasons": runtime["reasons"]})

    return {
        "name": case.name,
        "family": case.family,
        "polarity": case.polarity,
        "note": case.note,
        "source": case.source.lstrip("\n"),
        "modes": modes,
        "runtime": runtime,
        "passed": not reasons,
        "reasons": reasons,
    }


def version_string():
    res = run_cmd([BIN, "--version"], timeout=15)
    return (res["stdout"] + res["stderr"]).strip().splitlines()[:1]


def write_summary(records):
    total_checks = len(records) * len(MODES)
    checks_passed = sum(1 for r in records for m in MODES if r["modes"][m]["passed"])
    failed_records = [r for r in records if not r["passed"]]
    runtime_records = [r for r in records if r["runtime"] is not None]
    runtime_passed = sum(1 for r in runtime_records if r["runtime"]["passed"])

    failures = []
    for r in failed_records:
        for item in r["reasons"]:
            mode = item["mode"]
            if mode == "runtime":
                failures.append({
                    "case": r["name"], "family": r["family"], "mode": "runtime",
                    "polarity": r["polarity"], "reasons": item["reasons"],
                })
            else:
                m = r["modes"][mode]
                failures.append({
                    "case": r["name"], "family": r["family"], "mode": mode,
                    "polarity": r["polarity"],
                    "expected_reject": m["expected_reject"],
                    "expected_substring": m["expected_substring"],
                    "actual_reject": m["actual_reject"],
                    "actual_diagnostic": m["actual_diagnostic"][:600],
                    "reasons": m["reasons"],
                })

    families = Counter(r["family"] for r in records)
    by_family = {}
    for fam in sorted(families):
        fr = [r for r in records if r["family"] == fam]
        by_family[fam] = {
            "cases": len(fr),
            "passed": sum(1 for r in fr if r["passed"]),
            "failed": sum(1 for r in fr if not r["passed"]),
        }

    payload = {
        "suite": "interprocedural_safety",
        "generated_at": datetime.datetime.now().isoformat(timespec="seconds"),
        "binary": BIN,
        "binary_version": version_string(),
        "timeout_seconds": TIMEOUT,
        "workers": WORKERS,
        "modes": list(MODES),
        "totals": {
            "cases": len(records),
            "cases_passed": len(records) - len(failed_records),
            "cases_failed": len(failed_records),
            "mode_checks": total_checks,
            "mode_checks_passed": checks_passed,
            "mode_checks_failed": total_checks - checks_passed,
            "runtime_cases": len(runtime_records),
            "runtime_passed": runtime_passed,
            "runtime_failed": len(runtime_records) - runtime_passed,
            "failure_entries": len(failures),
            "soundness_gap_cases": sum(1 for r in records
                                       if r["polarity"] == "soundness-gap"),
            "soundness_gap_failures": sum(1 for r in failed_records
                                          if r["polarity"] == "soundness-gap"),
        },
        "by_family": by_family,
        "failures": failures,
        "cases": records,
    }
    (RESULTS / "summary.json").write_text(json.dumps(payload, indent=2) + "\n")

    lines = []
    add = lines.append
    add("C+ interprocedural-safety regression suite")
    add("=" * 60)
    add("binary       : %s" % BIN)
    add("version      : %s" % (payload["binary_version"] or ["(unknown)"])[0])
    add("generated    : %s" % payload["generated_at"])
    add("modes        : %s" % ", ".join(MODES))
    add("timeout      : %ss per subprocess" % TIMEOUT)
    add("")
    t = payload["totals"]
    add("SUMMARY")
    add("-" * 60)
    add("cases                : %d" % t["cases"])
    add("cases passed         : %d" % t["cases_passed"])
    add("cases failed         : %d" % t["cases_failed"])
    add("mode checks          : %d" % t["mode_checks"])
    add("mode checks passed   : %d" % t["mode_checks_passed"])
    add("mode checks failed   : %d" % t["mode_checks_failed"])
    add("runtime cases        : %d" % t["runtime_cases"])
    add("runtime passed       : %d" % t["runtime_passed"])
    add("runtime failed       : %d" % t["runtime_failed"])
    add("soundness-gap cases  : %d (%d failing as designed)"
        % (t["soundness_gap_cases"], t["soundness_gap_failures"]))
    add("")
    add("BY FAMILY")
    add("-" * 60)
    for fam, info in by_family.items():
        add("  %-18s cases=%2d passed=%2d failed=%2d"
            % (fam, info["cases"], info["passed"], info["failed"]))
    add("")
    add("FAILURES (exact case names)")
    add("-" * 60)
    if not failures:
        add("  none")
    else:
        for f in failures:
            add("  %-28s mode=%-10s family=%s" %
                (f["case"], f["mode"], f["family"]))
            for reason in f["reasons"]:
                add("      - %s" % reason)
            if f.get("expected_substring") is not None:
                add(f"      expected substring: {f['expected_substring']}")
            if f.get("actual_diagnostic"):
                first = f["actual_diagnostic"].splitlines()[0][:200]
                add(f"      actual diagnostic : {first}")
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

    print("interprocedural_safety: %d cases, %d modes, binary=%s"
          % (len(CASES), len(MODES), BIN))
    records = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for rec in pool.map(evaluate_case, CASES):
            records.append(rec)

    payload = write_summary(records)
    t = payload["totals"]
    print("cases: %d passed / %d failed" % (t["cases_passed"], t["cases_failed"]))
    print("mode checks: %d passed / %d failed"
          % (t["mode_checks_passed"], t["mode_checks_failed"]))
    print("runtime: %d passed / %d failed" % (t["runtime_passed"], t["runtime_failed"]))
    if payload["failures"]:
        print("failures (%d entries, %d cases):"
              % (t["failure_entries"], t["cases_failed"]))
        seen = set()
        for f in payload["failures"]:
            if f["case"] in seen:
                continue
            seen.add(f["case"])
            print("  - %s [%s/%s]" % (f["case"], f["mode"], f["family"]))
    print("summary: %s" % (RESULTS / "summary.txt"))
    return 1 if payload["failures"] else 0


if __name__ == "__main__":
    sys.exit(main())
