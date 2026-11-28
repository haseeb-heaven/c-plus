#!/usr/bin/env python3
"""Native runtime memory-safety regression suite for C+.

Every case is compiled and *executed* in two independent modes:

  * primary       -- ``c+ build --strict --sanitize=none`` (C+ runtime checks
                     only, no sanitizer).  This is the authoritative mode.
  * supplementary -- ``c+ build --strict --sanitize=address`` (ASan).  This is
                     an independent second witness, never a replacement.

Assertions are exact, never "bare nonzero":

  * a rejected program must exit with the exact compiler return code (1) AND
    contain a named ``[CPLUS-*]`` diagnostic substring;
  * a runtime abort must exit with the exact code (SIGABRT -> -6, or 101 for a
    ``_Exit`` fatal) AND contain the exact ``C+ error:`` / ``C+ runtime error:``
    message;
  * an ASan detection must exit with the exact code (-6) AND contain both the
    ``ERROR: AddressSanitizer: <kind>`` and ``SUMMARY: AddressSanitizer:
    <kind>`` lines;
  * a clean run must exit 0 AND contain no diagnostic marker AND (where the
    value is deterministic) the exact stdout substring.

No ``|| true``: every subprocess return code is classified, every subprocess
has a hard timeout, and timeouts / compiler crashes are hard failures.

Sanitizer builds recompile the C+ runtime, so the runner copies the live
``runtime/`` sources into a private ``results/root/`` once per run and points
``CPLUS_ROOT`` there.  That keeps the instrumented runtime matching the
compiler under test while isolating it from edits made mid-run.  The suite
never rebuilds ``build/c+``.

Results land in ``results/summary.{txt,json}`` and ``results/logs/*``.  The
case sources in ``cases/`` are regenerated from the manifest on every run.
Exit status is 0 only when every declared expectation matches.
"""

from __future__ import annotations

import concurrent.futures
import dataclasses
import datetime
import json
import os
import pathlib
import shlex
import shutil
import subprocess
import sys
from collections import Counter
from typing import Optional

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
CASES_DIR = HERE / "cases"
RESULTS = HERE / "results"
LOGS = RESULTS / "logs"
# Generated once per run from the live runtime tree: the instrumented-runtime
# object used by ASan builds must match the compiler binary under test, but is
# isolated from edits made while the suite is running.
SNAPSHOT = RESULTS / "root"

BIN = os.environ.get("CPLUS_BIN", str(pathlib.Path.cwd() / "build" / "c+"))
TIMEOUT = int(os.environ.get("RMS_TIMEOUT", "60"))
WORKERS = int(os.environ.get("RMS_WORKERS", "2"))
HOST_CLANG = os.environ.get("CPLUS_CHECK_CLANG", "/usr/bin/clang")
ASAN_CLANG = os.environ.get("CPLUS_ASAN_CLANG", "/opt/homebrew/opt/llvm/bin/clang")
SDKROOT = os.environ.get(
    "CPLUS_SDKROOT", "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"
)

PRIMARY = ("--strict", "--sanitize=none")
SUPPLEMENTARY = ("--strict", "--sanitize=address")
MODES = ("primary", "supplementary")

OK_LINE = "check: <SRC> ok"
MARKERS = ("AddressSanitizer", "C+ error:", "C+ runtime error:", "runtime error:")

ASAN_HEAP = (
    "ERROR: AddressSanitizer: heap-buffer-overflow",
    "SUMMARY: AddressSanitizer: heap-buffer-overflow",
)
ASAN_STACK = (
    "ERROR: AddressSanitizer: stack-buffer-overflow",
    "SUMMARY: AddressSanitizer: stack-buffer-overflow",
)

STD = "#include <stdio.h>\n"
STDLIB = "#include <stdlib.h>\n"
SIZE_T = "#include <stdint.h>\n"

# SIGABRT is reported by Python as -6 and by shells as 134.
SIGABRT = -6
FATAL_BOUNDS = 101


@dataclasses.dataclass(frozen=True)
class Case:
    name: str
    family: str
    kind: str  # safe | runtime_abort | asan_only | static_reject
    source: str
    note: str = ""
    stdout: tuple[str, ...] = ()          # exact stdout substrings (safe)
    diag: tuple[str, ...] = ()            # required run/compile diagnostic substrings
    exit_code: Optional[int] = None       # exact expected run exit code (runtime_abort)
    asan: tuple[str, ...] = ()            # required ASan substrings (asan_only)


def safe(name, family, source, stdout, note=""):
    return Case(name, family, "safe", source, note, tuple(stdout))


def runtime_abort(name, family, source, diag, exit_code, note=""):
    return Case(name, family, "runtime_abort", source, note,
                (), tuple(diag), exit_code)


def asan_only(name, family, source, asan, note=""):
    return Case(name, family, "asan_only", source, note,
                (), (), None, tuple(asan))


def static_reject(name, family, source, diag, note=""):
    return Case(name, family, "static_reject", source, note,
                (), tuple(diag))


CASES: list[Case] = [
    # ------------------------------------------------------------------
    # Stack bounds
    # ------------------------------------------------------------------
    safe("stack_bounds_ok", "stack_bounds", STD + r'''
static volatile int idx = 2;
int main(void){ int a[4]={1,2,3,4}; printf("v=%d\n", a[idx]); return 0; }
''', ("v=3",)),

    runtime_abort("stack_bounds_oob", "stack_bounds", STD + r'''
static volatile int idx = 7;
int main(void){ int a[4]={1,2,3,4}; printf("v=%d\n", a[idx]); return 0; }
''', ("C+ error: index 7 is outside the bounds of 'a' (length 4)",), SIGABRT),

    # ------------------------------------------------------------------
    # Heap bounds (C+ does not instrument pointer subscripts -> ASan witness)
    # ------------------------------------------------------------------
    safe("heap_bounds_ok", "heap_bounds", STD + STDLIB + r'''
static volatile int idx = 2;
int main(void){ int *a=malloc(16); if(!a)return 2; for(int i=0;i<4;i++)a[i]=i;
  printf("v=%d\n", a[idx]); free(a); return 0; }
''', ("v=2",)),

    asan_only("heap_bounds_oob", "heap_bounds", STD + STDLIB + r'''
static volatile int idx = 5;
int main(void){ int *a=malloc(16); if(!a)return 2; for(int i=0;i<4;i++)a[i]=i;
  printf("v=%d\n", a[idx]); free(a); return 0; }
''', ASAN_HEAP),

    # ------------------------------------------------------------------
    # Global bounds
    # ------------------------------------------------------------------
    safe("global_bounds_ok", "global_bounds", STD + r'''
static volatile int idx = 2;
int g[4]={1,2,3,4};
int main(void){ printf("v=%d\n", g[idx]); return 0; }
''', ("v=3",)),

    runtime_abort("global_bounds_oob", "global_bounds", STD + r'''
static volatile int idx = 8;
int g[4]={1,2,3,4};
int main(void){ printf("v=%d\n", g[idx]); return 0; }
''', ("C+ error: index 8 is outside the bounds of 'g' (length 4)",), SIGABRT),

    # ------------------------------------------------------------------
    # Array decay to a pointer parameter
    # ------------------------------------------------------------------
    safe("decay_ok", "array_decay", STD + r'''
static volatile int idx = 2;
static int first(int *p){ return p[idx]; }
int main(void){ int a[4]={1,2,3,4}; printf("v=%d\n", first(a)); return 0; }
''', ("v=3",)),

    asan_only("decay_oob", "array_decay", STD + r'''
static volatile int idx = 6;
static int first(int *p){ return p[idx]; }
int main(void){ int a[4]={1,2,3,4}; printf("v=%d\n", first(a)); return 0; }
''', ASAN_STACK),

    # ------------------------------------------------------------------
    # Interior pointers
    # ------------------------------------------------------------------
    safe("interior_ok", "interior_pointers", STD + r'''
static volatile int idx = 1;
int main(void){ int a[8]={0,1,2,3,4,5,6,7}; int *p=&a[2]; printf("v=%d\n", p[idx]); return 0; }
''', ("v=3",)),

    asan_only("interior_oob", "interior_pointers", STD + STDLIB + r'''
static volatile int idx = 5;
int main(void){ int *a=malloc(4*sizeof(int)); if(!a)return 2; for(int i=0;i<4;i++)a[i]=i;
  int *p=&a[2]; printf("v=%d\n", p[idx]); free(a); return 0; }
''', ASAN_HEAP),

    # ------------------------------------------------------------------
    # Pointer arithmetic / pointer formation
    # ------------------------------------------------------------------
    safe("ptr_arith_ok", "pointer_arithmetic", STD + r'''
static volatile int n = 2;
int main(void){ int a[4]={1,2,3,4}; int *p=a; p+=n; printf("v=%d\n", *p); return 0; }
''', ("v=3",)),

    runtime_abort("ptr_arith_oob", "pointer_arithmetic", STD + r'''
static volatile int n = 10;
int main(void){ int a[4]={1,2,3,4}; int *p=a; printf("v=%d\n", *(p+n)); return 0; }
''', ("C+ runtime error: out-of-bounds pointer formation",
      "pointer offset 10 is outside [0, 4] elements"), FATAL_BOUNDS),

    # ------------------------------------------------------------------
    # Pointer-to-pointer
    # ------------------------------------------------------------------
    safe("ptr2_ok", "pointer_to_pointer", STD + r'''
static volatile int idx = 2;
static int rd(int **pp){ return (*pp)[idx]; }
int main(void){ int a[4]={1,2,3,4}; int *p=a; printf("v=%d\n", rd(&p)); return 0; }
''', ("v=3",)),

    asan_only("ptr2_oob", "pointer_to_pointer", STD + r'''
static volatile int idx = 6;
static void wr(int **pp){ (*pp)[idx]=1; }
int main(void){ int a[4]={0}; int *p=a; wr(&p); printf("v=%d\n", a[0]); return 0; }
''', ASAN_STACK),

    # ------------------------------------------------------------------
    # Casts
    # ------------------------------------------------------------------
    safe("cast_ok", "casts", STD + r'''
static volatile int idx = 2;
int main(void){ int a[4]={1,2,3,4}; void *v=a; int *p=(int*)v; printf("v=%d\n", p[idx]); return 0; }
''', ("v=3",)),

    # Wave 1 hardening: this cast-laundered OOB is now rejected statically
    # by the STRICT bounds checker ([CPLUS-BOUNDS]). Previously it was an
    # asan_only case (accepted by c+, caught only by sanitizer). Requiring
    # static rejection is a strictly stronger expectation, not a weakened one.
    static_reject("cast_oob", "casts", STD + r'''
static volatile int idx = 6;
int main(void){ int a[4]={1,2,3,4}; void *v=a; int *p=(int*)v; printf("v=%d\n", p[idx]); return 0; }
''', ("[CPLUS-BOUNDS]",)),

    # ------------------------------------------------------------------
    # Function parameters and returned pointers
    # ------------------------------------------------------------------
    safe("param_return_ok", "function_params_returns", STD + STDLIB + r'''
static int *id(int *p){ return p; }
int main(void){ int *a=malloc(16); if(!a)return 2; for(int i=0;i<4;i++)a[i]=i;
  int *q=id(a); printf("v=%d\n", q[2]); free(a); return 0; }
''', ("v=2",)),

    asan_only("param_oob", "function_params_returns", STD + r'''
static volatile int idx = 5;
static int rd(int *p){ return p[idx]; }
int main(void){ int a[4]={1,2,3,4}; printf("v=%d\n", rd(a)); return 0; }
''', ASAN_STACK),

    asan_only("param_heap_oob", "function_params_returns", STD + STDLIB + r'''
static volatile int idx = 5;
static int rd(int *p){ return p[idx]; }
int main(void){ int *a=malloc(16); if(!a)return 2; for(int i=0;i<4;i++)a[i]=i;
  printf("v=%d\n", rd(a)); free(a); return 0; }
''', ASAN_HEAP),

    # ------------------------------------------------------------------
    # Struct pointer fields
    # ------------------------------------------------------------------
    safe("struct_ptr_ok", "struct_pointer_fields", STD + r'''
struct B{ int *p; };
static int rd(struct B *b){ return b->p[2]; }
int main(void){ int a[4]={1,2,3,4}; struct B b; b.p=a; printf("v=%d\n", rd(&b)); return 0; }
''', ("v=3",)),

    asan_only("struct_ptr_oob", "struct_pointer_fields", STD + STDLIB + r'''
static volatile int idx = 4;
struct B{ int *p; };
static void wr(struct B *b){ b->p[idx]=1; }
int main(void){ int *a=malloc(4*sizeof(int)); if(!a)return 2; for(int i=0;i<4;i++)a[i]=i;
  struct B b; b.p=a; wr(&b); printf("v=%d\n", a[0]); free(a); return 0; }
''', ASAN_HEAP),

    # ------------------------------------------------------------------
    # malloc / calloc / realloc / free consistency
    # ------------------------------------------------------------------
    safe("alloc_consistency_ok", "alloc_free_consistency", STD + STDLIB + r'''
int main(void){ int *p=calloc(4,sizeof(int)); if(!p)return 2; p[3]=7;
  printf("v=%d\n", p[3]); free(p);
  int *q=malloc(16); if(!q)return 2; q[0]=9; printf("w=%d\n", q[0]); free(q);
  printf("ok\n"); return 0; }
''', ("v=7", "w=9", "ok")),

    static_reject("double_free", "alloc_free_consistency", STDLIB + r'''
int main(void){ int *p=malloc(16); if(!p)return 2; free(p); free(p); return 0; }
''', ("error: [CPLUS-OWNER] double free of 'p'",)),

    static_reject("uaf", "alloc_free_consistency", STD + STDLIB + r'''
int main(void){ int *p=malloc(16); if(!p)return 2; free(p); printf("v=%d\n", p[0]); return 0; }
''', ("error: [CPLUS-LIFETIME]",
      "use-after-free: subscript of released pointer")),

    static_reject("realloc_stale", "alloc_free_consistency", STD + STDLIB + r'''
int main(void){ int *p=malloc(16); if(!p)return 2; for(int i=0;i<4;i++)p[i]=i;
  int *q=realloc(p,64); if(!q){free(p);return 2;} printf("v=%d\n", p[0]); free(q); return 0; }
''', ("error: [CPLUS-LIFETIME]",
      "use-after-free: subscript of released pointer")),

    static_reject("invalid_free_interior", "alloc_free_consistency", STDLIB + r'''
int main(void){ int *p=malloc(64); if(!p)return 2; free(p+2); return 0; }
''', ("error: [CPLUS-OWNER] invalid free of interior pointer 'p' via free",)),

    # ------------------------------------------------------------------
    # Allocation-size consistency
    # ------------------------------------------------------------------
    static_reject("alloc_wrap", "allocation_consistency", STDLIB + SIZE_T + r'''
int main(void){ size_t n=SIZE_MAX/2; int *p=malloc(n*sizeof(int)); if(!p)return 2; free(p); return 0; }
''', ("error: [CPLUS-FFI] allocation size wraps size_t",)),

    static_reject("alloc_zero", "allocation_consistency", STD + STDLIB + r'''
int main(void){ int *p=malloc(0); printf("z=%p\n",(void*)p); free(p); return 0; }
''', ("error: [CPLUS-OWNER] zero-size allocation is implementation-defined",)),
]


# ---------------------------------------------------------------------------
# subprocess plumbing
# ---------------------------------------------------------------------------

def run_cmd(argv, env, timeout=TIMEOUT):
    try:
        p = subprocess.run(argv, capture_output=True, text=True,
                           timeout=timeout, env=env)
        return {"rc": p.returncode, "out": p.stdout, "err": p.stderr,
                "timeout": False}
    except subprocess.TimeoutExpired as exc:
        out, err = exc.stdout or b"", exc.stderr or b""
        if isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        if isinstance(err, bytes):
            err = err.decode("utf-8", "replace")
        return {"rc": None, "out": out, "err": err, "timeout": True}
    except OSError as exc:
        return {"rc": None, "out": "", "err": "OS ERROR: %s" % exc,
                "timeout": False}


def normalise(text: str, path: pathlib.Path) -> str:
    text = text.replace(str(path), "<SRC>")
    text = text.replace(str(path.with_suffix("")), "<SRC>")
    return text.strip()


def primary_env():
    e = dict(os.environ)
    e["CPLUS_CLANG"] = HOST_CLANG
    e.pop("CPLUS_ROOT", None)
    return e


def asan_env():
    e = dict(os.environ)
    e["CPLUS_CLANG"] = ASAN_CLANG
    e["CPLUS_ROOT"] = str(SNAPSHOT)
    return e


def prepare_snapshot():
    """Copy the live runtime sources into a private root for this run."""
    rels = ("runtime/cplus_runtime.c",
            "runtime/include/cplus/cplus.h",
            "runtime/include/cplus/clog.h")
    missing = [str(REPO / r) for r in rels if not (REPO / r).exists()]
    if missing:
        raise RuntimeError("missing live runtime sources: %s" % missing)
    if SNAPSHOT.exists():
        shutil.rmtree(SNAPSHOT)
    for rel in rels:
        dst = SNAPSHOT / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(REPO / rel, dst)
    # findSourceRoot() requires this marker to honour CPLUS_ROOT.
    (SNAPSHOT / "CMakeLists.txt").write_text("# runtime snapshot for suite\n")


def write_log(name, suffix, argv, res, norm):
    (LOGS / ("%s.%s.txt" % (name, suffix))).write_text(
        "command: %s\nreturncode: %s\ntimed_out: %s\n\n%s\n"
        % (shlex.join(argv), res["rc"], res["timeout"], norm))


def require(cond, reasons, msg):
    if not cond:
        reasons.append(msg)


def has_marker(text):
    return tuple(m for m in MARKERS if m in text)


# ---------------------------------------------------------------------------
# per-mode evaluation
# ---------------------------------------------------------------------------

def evaluate_mode(case: Case, mode: str, src: pathlib.Path):
    env = primary_env() if mode == "primary" else asan_env()
    flags = PRIMARY if mode == "primary" else SUPPLEMENTARY
    reasons = []
    record = {"mode": mode, "reasons": reasons, "steps": {}}

    # Static check runs in the primary mode only.  The supplementary mode is
    # validated through its instrumented build+run (check never instruments).
    if mode == "primary":
        argv = [BIN, "check", *flags, str(src)]
        chk = run_cmd(argv, env)
        cdiag = normalise(chk["out"] + chk["err"], src)
        write_log(case.name, "primary.check", argv, chk, cdiag)
        record["steps"]["check"] = {"rc": chk["rc"], "timeout": chk["timeout"],
                                    "diagnostic": cdiag}
        require(not chk["timeout"], reasons, "check TIMEOUT after %ds" % TIMEOUT)
        if case.kind == "static_reject":
            require(chk["rc"] == 1, reasons,
                    "check rc=%s != expected 1 (reject)" % chk["rc"])
            for sub in case.diag:
                require(sub in cdiag, reasons,
                        "check missing exact diagnostic %r" % sub)
        else:
            require(chk["rc"] == 0, reasons,
                    "check rc=%s != expected 0 (accept)" % chk["rc"])
            require(OK_LINE in cdiag, reasons,
                    "check missing acceptance line %r" % OK_LINE)

    binary = CASES_DIR / ("%s.%s.bin" % (case.name, mode))
    if binary.exists():
        binary.unlink()
    extra = ["--", "-isysroot", SDKROOT] if mode == "supplementary" else []
    argv = [BIN, "build", *flags, str(src), "-o", str(binary)] + extra
    bld = run_cmd(argv, env)
    bdiag = normalise(bld["out"] + bld["err"], src)
    write_log(case.name, "%s.build" % mode, argv, bld, bdiag)
    record["steps"]["build"] = {"rc": bld["rc"], "timeout": bld["timeout"],
                                "command": shlex.join(argv), "diagnostic": bdiag}

    if bld["timeout"] or (bld["rc"] is not None and bld["rc"] < 0):
        reasons.append("build %s (rc=%s)" %
                       ("TIMEOUT" if bld["timeout"] else "CRASH", bld["rc"]))
        record["result"] = "BUILD_FAIL"
        return record

    if case.kind == "static_reject":
        require(bld["rc"] == 1, reasons,
                "build rc=%s != expected 1 (reject)" % bld["rc"])
        for sub in case.diag:
            require(sub in bdiag, reasons,
                    "build missing exact diagnostic %r" % sub)
        require(not binary.exists(), reasons,
                "rejected case unexpectedly produced a binary")
        record["result"] = "REJECT"
        return record

    require(bld["rc"] == 0, reasons, "build rc=%s != expected 0" % bld["rc"])
    if not binary.exists():
        reasons.append("build produced no binary")
        record["result"] = "NO_BINARY"
        return record
    record["result"] = "BUILT"

    runr = run_cmd([str(binary)], env)
    rout = normalise(runr["out"] + runr["err"], src)
    write_log(case.name, "%s.run" % mode, [str(binary)], runr, rout)
    record["steps"]["run"] = {"rc": runr["rc"], "timeout": runr["timeout"],
                              "output": rout}
    if runr["timeout"]:
        reasons.append("run TIMEOUT after %ds" % TIMEOUT)
        record["result"] = "RUN_TIMEOUT"
        return record

    markers = has_marker(rout)
    if case.kind == "safe":
        require(runr["rc"] == 0, reasons,
                "run rc=%s != expected 0" % runr["rc"])
        require(not markers, reasons, "unexpected diagnostic markers %r" % (markers,))
        for sub in case.stdout:
            require(sub in rout, reasons, "run missing stdout %r" % sub)
        record["result"] = "CLEAN"
    elif case.kind == "runtime_abort":
        require(runr["rc"] == case.exit_code, reasons,
                "run rc=%s != expected %s" % (runr["rc"], case.exit_code))
        for sub in case.diag:
            require(sub in rout, reasons, "run missing exact diagnostic %r" % sub)
        require(any(m in rout for m in ("C+ error:", "C+ runtime error:")),
                reasons, "run missing C+ runtime diagnostic prefix")
        record["result"] = "RUNTIME_ABORT"
    elif case.kind == "asan_only":
        if mode == "primary":
            # Primary is the authoritative clean control: the OOB lands in
            # accessible memory, so stdout is intentionally not asserted.
            require(runr["rc"] == 0, reasons,
                    "primary run rc=%s != expected 0" % runr["rc"])
            require(not markers, reasons,
                    "primary run has diagnostic markers %r" % (markers,))
            record["result"] = "PRIMARY_CLEAN"
        else:
            require(runr["rc"] == SIGABRT, reasons,
                    "ASan run rc=%s != expected %s" % (runr["rc"], SIGABRT))
            for sub in case.asan:
                require(sub in rout, reasons,
                        "ASan run missing exact diagnostic %r" % sub)
            record["result"] = "ASAN_DETECTED"
    return record


def evaluate_case(case: Case):
    src = CASES_DIR / (case.name + ".c")
    src.write_text(case.source.lstrip("\n"))
    modes = {m: evaluate_mode(case, m, src) for m in MODES}
    reasons = [{"mode": m, "reasons": modes[m]["reasons"]}
               for m in MODES if modes[m]["reasons"]]
    return {"name": case.name, "family": case.family, "kind": case.kind,
            "note": case.note, "source": case.source.lstrip("\n"),
            "modes": modes, "passed": not reasons, "reasons": reasons}


# ---------------------------------------------------------------------------
# reporting
# ---------------------------------------------------------------------------

def version_string():
    res = run_cmd([BIN, "--version"], timeout=15,
                  env={**os.environ, "CPLUS_ROOT": str(SNAPSHOT)})
    return (res["out"] + res["err"]).strip().splitlines()[:1]


def write_summary(records):
    cases_failed = [r for r in records if not r["passed"]]
    failures = []
    for r in cases_failed:
        for item in r["reasons"]:
            failures.append({"case": r["name"], "family": r["family"],
                             "kind": r["kind"], "mode": item["mode"],
                             "reasons": item["reasons"]})

    by_family = {}
    for fam in sorted({r["family"] for r in records}):
        fr = [r for r in records if r["family"] == fam]
        by_family[fam] = {"cases": len(fr),
                          "passed": sum(1 for r in fr if r["passed"]),
                          "failed": sum(1 for r in fr if not r["passed"])}

    payload = {
        "suite": "runtime_memory_safety",
        "generated_at": datetime.datetime.now().isoformat(timespec="seconds"),
        "binary": BIN,
        "binary_version": version_string(),
        "checks": {
            "primary_flags": list(PRIMARY),
            "supplementary_flags": list(SUPPLEMENTARY),
            "asan_clang": ASAN_CLANG,
            "sdkroot": SDKROOT,
            "runtime_snapshot": str(SNAPSHOT),
        },
        "timeout_seconds": TIMEOUT,
        "workers": WORKERS,
        "modes": list(MODES),
        "totals": {
            "cases": len(records),
            "cases_passed": len(records) - len(cases_failed),
            "cases_failed": len(cases_failed),
            "mode_checks": len(records) * len(MODES),
            "failure_entries": len(failures),
            "by_kind": dict(Counter(r["kind"] for r in records)),
        },
        "by_family": by_family,
        "failures": failures,
        "cases": records,
    }
    (RESULTS / "summary.json").write_text(json.dumps(payload, indent=2) + "\n")

    t = payload["totals"]
    lines = []
    add = lines.append
    add("C+ runtime memory-safety regression suite")
    add("=" * 64)
    add("binary        : %s" % BIN)
    add("version       : %s" % (payload["binary_version"] or ["(unknown)"])[0])
    add("generated     : %s" % payload["generated_at"])
    add("primary       : c+ build %s" % " ".join(PRIMARY))
    add("supplementary : c+ build %s (ASan, live runtime snapshotted per run)" %
        " ".join(SUPPLEMENTARY))
    add("timeout       : %ss per subprocess" % TIMEOUT)
    add("")
    add("SUMMARY")
    add("-" * 64)
    add("cases             : %d" % t["cases"])
    add("cases passed      : %d" % t["cases_passed"])
    add("cases failed      : %d" % t["cases_failed"])
    add("mode checks       : %d (2 per case)" % t["mode_checks"])
    add("failure entries   : %d" % t["failure_entries"])
    add("by kind           : %s" % ", ".join(
        "%s=%d" % (k, t["by_kind"][k]) for k in sorted(t["by_kind"])))
    add("")
    add("BY FAMILY")
    add("-" * 64)
    for fam, info in by_family.items():
        add("  %-26s cases=%2d passed=%2d failed=%2d"
            % (fam, info["cases"], info["passed"], info["failed"]))
    add("")
    add("FAILURES (exact case names, modes and reasons)")
    add("-" * 64)
    if not failures:
        add("  none")
    else:
        for f in failures:
            add("  %s [%s / %s / %s]" %
                (f["case"], f["mode"], f["family"], f["kind"]))
            for reason in f["reasons"]:
                add("      - %s" % reason)
    add("")
    add("Full records: results/summary.json ; logs: results/logs/")
    (RESULTS / "summary.txt").write_text("\n".join(lines) + "\n")
    return payload


def main():
    if not pathlib.Path(BIN).exists():
        print("error: CPLUS_BIN not found: %s" % BIN, file=sys.stderr)
        return 2
    try:
        prepare_snapshot()
    except (RuntimeError, OSError) as exc:
        print("error: cannot prepare runtime snapshot: %s" % exc,
              file=sys.stderr)
        return 2
    if not pathlib.Path(ASAN_CLANG).exists():
        print("error: ASan clang not found: %s (set CPLUS_ASAN_CLANG)" %
              ASAN_CLANG, file=sys.stderr)
        return 2

    names = [c.name for c in CASES]
    if len(names) != len(set(names)):
        raise SystemExit("duplicate case name in manifest")

    CASES_DIR.mkdir(parents=True, exist_ok=True)
    LOGS.mkdir(parents=True, exist_ok=True)

    print("runtime_memory_safety: %d cases, 2 modes each, binary=%s"
          % (len(CASES), BIN))
    records = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for rec in pool.map(evaluate_case, CASES):
            records.append(rec)

    payload = write_summary(records)
    t = payload["totals"]
    print("cases: %d passed / %d failed" % (t["cases_passed"], t["cases_failed"]))
    print("failure entries: %d" % t["failure_entries"])
    if payload["failures"]:
        seen = set()
        for f in payload["failures"]:
            if f["case"] in seen:
                continue
            seen.add(f["case"])
            print("  - %s [%s / %s]" % (f["case"], f["mode"], f["family"]))
    print("summary: %s" % (RESULTS / "summary.txt"))
    return 1 if payload["failures"] else 0


if __name__ == "__main__":
    sys.exit(main())
