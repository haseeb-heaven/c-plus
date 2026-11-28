#!/usr/bin/env python3
"""Native C+ vs Rust memory-safety violation comparison suite.

Seven violation families are represented by one equivalent C+ case and one
equivalent Rust case each (14 cases).  Both toolchains are invoked with
sanitizers disabled:

  * C+   ``c+ check/build --strict --sanitize=none`` (C+ runtime checks only)
  * Rust ``rustc --edition 2021 -C overflow-checks=on`` (no ``-Zsanitizer``;
          Rust's debug overflow checks are language debug assertions, not a
          compiler sanitizer)

Nonzero exits are never accepted as proof on their own.  Every case declares
its exact expected stage and evidence:

  * ``compile_reject`` - compiler rc 1, named diagnostic substring(s), and NO
    executable produced.  For C+ both ``check`` and ``build`` are asserted.
  * ``runtime_trap``   - compile rc 0 and executable produced, then run rc must
    equal the exact expected code (C+ SIGABRT = -6, Rust panic = 101) and the
    exact diagnostic substring(s) must be present.

A case that merely exits nonzero without its declared diagnostic/code fails.
Sanitizer banners (ASan/UBSan/MSan/TSan/LSan) in any output are hard failures.
Every subprocess has a hard timeout; timeouts and compiler crashes are failures.

The suite never rebuilds ``build/c+``, never edits existing files, and never
commits.  It writes only new files below this directory:

  cases/<name>.(c|rs)          regenerated sources
  results/summary.txt          human-readable counts + exact failure reasons
  results/summary.json         full machine-readable records
  results/logs/<case>.<step>.txt   exact command, rc and full output per step

Run:

    cd <repo-root>
    python3 tests/compiler/memory_safety_compare/run_suite.py

Optional env: CPLUS_BIN (default <repo>/build/c+), MSC_TIMEOUT (default 60s),
MSC_WORKERS (default 2), RUSTC (default ``rustc``), CPLUS_CLANG (default
``/usr/bin/clang``), MSC_RUSTC_FLAGS (override the rustc flags string).
"""

from __future__ import annotations

import concurrent.futures
import dataclasses
import datetime
import json
import os
import pathlib
import shlex
import subprocess
import sys
from collections import Counter
from typing import Optional

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
CASES_DIR = HERE / "cases"
RESULTS = HERE / "results"
LOGS = RESULTS / "logs"

BIN = os.environ.get("CPLUS_BIN", str(REPO / "build" / "c+"))
RUSTC = os.environ.get("RUSTC", "rustc")
RUST_FLAGS = shlex.split(
    os.environ.get("MSC_RUSTC_FLAGS", "--edition 2021 -C overflow-checks=on")
)
TIMEOUT = int(os.environ.get("MSC_TIMEOUT", "60"))
WORKERS = int(os.environ.get("MSC_WORKERS", "2"))
HOST_CLANG = os.environ.get("CPLUS_CLANG", "/usr/bin/clang")

CPLUS_FLAGS = ("--strict", "--sanitize=none")
OK_LINE_PREFIX = "check: <SRC> ok"

# Exact process return codes (Python/subprocess convention on macOS).
SIGABRT = -6      # C+ runtime abort()
RUST_PANIC = 101  # Rust main-thread panic (unwinds, then exits 101)

# A bare nonzero exit is not evidence.  These are only *sanity* markers: if a
# sanitizer banner appears the run is a hard failure, because the suite must
# demonstrate the language's own checks with sanitizers disabled.
SANITIZER_MARKERS = (
    "AddressSanitizer", "UndefinedBehaviorSanitizer", "runtime error:",
    "MemorySanitizer", "ThreadSanitizer", "LeakSanitizer",
)

CATEGORIES = (
    "out_of_bounds_index",
    "use_after_free",
    "double_free",
    "invalid_free",
    "null_deref",
    "integer_overflow",
    "invalid_shift",
)


@dataclasses.dataclass(frozen=True)
class Case:
    name: str
    category: str
    lang: str                    # cplus | rust
    outcome: str                 # compile_reject | runtime_trap
    source: str
    compile_rc: int              # expected rc of check/build (C+) or rustc
    compile_diag: tuple[str, ...]
    exit_code: Optional[int]     # exact run rc, only for runtime_trap
    run_diag: tuple[str, ...]
    note: str


CASES: list[Case] = [
    # ------------------------------------------------------------------
    # 1. Out-of-bounds index -- both languages trap at runtime.  The index is
    #    hidden from constant folding (volatile in C+, black_box in Rust) so
    #    the violation is detected by runtime checks, not by compilation.
    # ------------------------------------------------------------------
    Case(
        name="oob_index__cplus", category="out_of_bounds_index", lang="cplus",
        outcome="runtime_trap", compile_rc=0, compile_diag=(),
        exit_code=SIGABRT,
        run_diag=("C+ error: index 7 is outside the bounds of 'a' (length 4)",),
        note="volatile index 7 into int a[4]; C+ runtime bounds check aborts",
        source=r'''
#include <stdio.h>

static volatile int idx = 7;
static int a[4] = {1, 2, 3, 4};

int main(void) {
    printf("v=%d\n", a[idx]);
    return 0;
}
''',
    ),
    Case(
        name="oob_index__rust", category="out_of_bounds_index", lang="rust",
        outcome="runtime_trap", compile_rc=0, compile_diag=(),
        exit_code=RUST_PANIC,
        run_diag=("thread 'main'",
                  "index out of bounds: the len is 4 but the index is 7"),
        note="black_box index 7 into a Vec of len 4; Rust slice index panics",
        source=r'''
fn main() {
    let v = vec![10, 20, 30, 40];
    let idx = std::hint::black_box(7usize);
    println!("{}", v[idx]);
}
''',
    ),

    # ------------------------------------------------------------------
    # 2. Use-after-free -- both languages reject at compile time.
    # ------------------------------------------------------------------
    Case(
        name="uaf__cplus", category="use_after_free", lang="cplus",
        outcome="compile_reject", compile_rc=1,
        compile_diag=("error: [CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] "
                      "use-after-free: subscript of released pointer",),
        exit_code=None, run_diag=(),
        note="subscript of a released pointer is statically rejected",
        source=r'''
#include <stdlib.h>

int main(void) {
    int *p = malloc(16);
    if (!p) return 2;
    free(p);
    return p[0];
}
''',
    ),
    Case(
        name="uaf__rust", category="use_after_free", lang="rust",
        outcome="compile_reject", compile_rc=1,
        compile_diag=("error[E0505]: cannot move out of `v` because it is borrowed",
                      "borrow later used here"),
        exit_code=None, run_diag=(),
        note="borrow checker rejects dropping a Box while a &mut borrow is live",
        source=r'''
fn main() {
    let mut v = Box::new(1i32);
    let p = &mut *v;
    drop(v);
    *p = 2;
}
''',
    ),

    # ------------------------------------------------------------------
    # 3. Double-free -- both languages reject at compile time.
    # ------------------------------------------------------------------
    Case(
        name="double_free__cplus", category="double_free", lang="cplus",
        outcome="compile_reject", compile_rc=1,
        compile_diag=("error: [CPLUS-OWNER] double free of 'p'",),
        exit_code=None, run_diag=(),
        note="ownership analysis rejects the second free of the same owner",
        source=r'''
#include <stdlib.h>

int main(void) {
    int *p = malloc(16);
    if (!p) return 2;
    free(p);
    free(p);
    return 0;
}
''',
    ),
    Case(
        name="double_free__rust", category="double_free", lang="rust",
        outcome="compile_reject", compile_rc=1,
        compile_diag=("error[E0382]: use of moved value: `v`",
                      "value used here after move"),
        exit_code=None, run_diag=(),
        note="second drop uses a Box already moved into the first drop",
        source=r'''
fn main() {
    let v = Box::new(1i32);
    drop(v);
    drop(v);
}
''',
    ),

    # ------------------------------------------------------------------
    # 4. Invalid free (freeing an interior pointer) -- compile time in both.
    #    Rust has no raw free: its deallocation API requires an owned Box, so
    #    an interior raw pointer is rejected by the type system (E0308).
    # ------------------------------------------------------------------
    Case(
        name="invalid_free__cplus", category="invalid_free", lang="cplus",
        outcome="compile_reject", compile_rc=1,
        compile_diag=("error: [CPLUS-OWNER] invalid free of interior pointer "
                      "'p' via free",),
        exit_code=None, run_diag=(),
        note="free(p+2) on a malloc owner is statically rejected",
        source=r'''
#include <stdlib.h>

int main(void) {
    int *p = malloc(64);
    if (!p) return 2;
    free(p + 2);
    return 0;
}
''',
    ),
    Case(
        name="invalid_free__rust", category="invalid_free", lang="rust",
        outcome="compile_reject", compile_rc=1,
        compile_diag=("error[E0308]: mismatched types",
                      "expected `Box<i32>`, found `*mut i32`"),
        exit_code=None, run_diag=(),
        note="safe deallocation requires Box ownership; interior *mut is not one",
        source=r'''
fn free_it(owned: Box<i32>) { drop(owned); }

fn main() {
    let arr = [1i32, 2, 3];
    let p: *mut i32 = &arr[1] as *const i32 as *mut i32;
    free_it(p);
}
''',
    ),

    # ------------------------------------------------------------------
    # 5. Null dereference -- C+ rejects the constant dereference at compile
    #    time; Rust has no null references, and the equivalent None unwrap
    #    panics at runtime.
    # ------------------------------------------------------------------
    Case(
        name="null_deref__cplus", category="null_deref", lang="cplus",
        outcome="compile_reject", compile_rc=1,
        compile_diag=("error: [CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] "
                      "null pointer dereference",),
        exit_code=None, run_diag=(),
        note="dereference of a provably null pointer is statically rejected",
        source=r'''
int main(void) {
    int *p = 0;
    return *p;
}
''',
    ),
    Case(
        name="null_deref__rust", category="null_deref", lang="rust",
        outcome="runtime_trap", compile_rc=0, compile_diag=(),
        exit_code=RUST_PANIC,
        run_diag=("thread 'main'",
                  "called `Option::unwrap()` on a `None` value"),
        note="Rust expresses 'null' as Option; unwrapping None panics",
        source=r'''
fn main() {
    let p: Option<&i32> = None;
    println!("{}", p.unwrap());
}
''',
    ),

    # ------------------------------------------------------------------
    # 6. Signed integer overflow -- C+ rejects the constant overflow;
    #    Rust's debug overflow check panics at runtime (no sanitizer).
    # ------------------------------------------------------------------
    Case(
        name="integer_overflow__cplus", category="integer_overflow",
        lang="cplus", outcome="compile_reject", compile_rc=1,
        compile_diag=("error: [CPLUS-ARITH] signed integer overflow in addition",),
        exit_code=None, run_diag=(),
        note="constant signed overflow is statically rejected",
        source=r'''
int main(void) {
    int x = 2147483647 + 1;
    return x;
}
''',
    ),
    Case(
        name="integer_overflow__rust", category="integer_overflow", lang="rust",
        outcome="runtime_trap", compile_rc=0, compile_diag=(),
        exit_code=RUST_PANIC,
        run_diag=("thread 'main'", "attempt to add with overflow"),
        note="operands hidden by black_box; debug overflow check panics",
        source=r'''
fn main() {
    let a = std::hint::black_box(i32::MAX);
    let b = std::hint::black_box(1);
    println!("{}", a + b);
}
''',
    ),

    # ------------------------------------------------------------------
    # 7. Invalid shift -- C+ rejects the constant out-of-range shift;
    #    Rust's debug shift check panics at runtime (no sanitizer).
    # ------------------------------------------------------------------
    Case(
        name="invalid_shift__cplus", category="invalid_shift", lang="cplus",
        outcome="compile_reject", compile_rc=1,
        compile_diag=("error: [CPLUS-FFI] shift count 32 is not less than the "
                      "width 32 of the left operand",),
        exit_code=None, run_diag=(),
        note="constant shift >= width of left operand is statically rejected",
        source=r'''
int main(void) {
    int x = 1 << 32;
    return x;
}
''',
    ),
    Case(
        name="invalid_shift__rust", category="invalid_shift", lang="rust",
        outcome="runtime_trap", compile_rc=0, compile_diag=(),
        exit_code=RUST_PANIC,
        run_diag=("thread 'main'", "attempt to shift left with overflow"),
        note="shift amount hidden by black_box; debug shift check panics",
        source=r'''
fn main() {
    let n = std::hint::black_box(32u32);
    println!("{}", 1i32 << n);
}
''',
    ),
]


# ---------------------------------------------------------------------------
# subprocess plumbing
# ---------------------------------------------------------------------------

def run_cmd(argv, env, timeout=TIMEOUT):
    try:
        p = subprocess.run([str(a) for a in argv], capture_output=True,
                           text=True, timeout=timeout, env=env)
        return {"rc": p.returncode, "out": p.stdout, "err": p.stderr,
                "timeout": False, "crash": False}
    except subprocess.TimeoutExpired as exc:
        out, err = exc.stdout or b"", exc.stderr or b""
        if isinstance(out, bytes):
            out = out.decode("utf-8", "replace")
        if isinstance(err, bytes):
            err = err.decode("utf-8", "replace")
        return {"rc": None, "out": out, "err": err,
                "timeout": True, "crash": False}
    except OSError as exc:
        return {"rc": None, "out": "", "err": "OS ERROR: %s" % exc,
                "timeout": False, "crash": True}


def normalise(text: str, path: pathlib.Path) -> str:
    return text.replace(str(path), "<SRC>").strip()


def cplus_env():
    e = dict(os.environ)
    e["CPLUS_CLANG"] = HOST_CLANG
    e.pop("CPLUS_ROOT", None)
    return e


def write_log(name, step, argv, res, norm):
    (LOGS / ("%s.%s.txt" % (name, step))).write_text(
        "command: %s\nreturncode: %s\ntimed_out: %s\n\n%s\n"
        % (shlex.join(str(a) for a in argv), res["rc"], res["timeout"], norm))


def require(cond, reasons, msg):
    if not cond:
        reasons.append(msg)


def sanitizer_markers(text):
    return tuple(m for m in SANITIZER_MARKERS if m in text)


def step_record(argv, res, norm, extra=None):
    rec = {"command": shlex.join(str(a) for a in argv), "rc": res["rc"],
           "timed_out": res["timeout"], "crashed": res["crash"],
           "output": norm}
    if extra:
        rec.update(extra)
    return rec


# ---------------------------------------------------------------------------
# evaluation
# ---------------------------------------------------------------------------

def evaluate(case: Case):
    ext = "c" if case.lang == "cplus" else "rs"
    src = CASES_DIR / ("%s.%s" % (case.name, ext))
    src.write_text(case.source.lstrip("\n"))
    binary = CASES_DIR / ("%s.bin" % case.name)

    env = cplus_env()
    reasons = []
    record = {"name": case.name, "category": case.category,
              "lang": case.lang, "outcome": case.outcome,
              "note": case.note, "source": case.source.lstrip("\n"),
              "steps": {}, "reasons": reasons}

    if binary.exists():
        binary.unlink()

    if case.lang == "cplus":
        # Step 1: c+ check (static front end), asserted like a build.
        check_argv = [BIN, "check", *CPLUS_FLAGS, str(src)]
        chk = run_cmd(check_argv, env)
        cdiag = normalise(chk["out"] + chk["err"], src)
        write_log(case.name, "check", check_argv, chk, cdiag)
        record["steps"]["check"] = step_record(check_argv, chk, cdiag)
        require(not chk["timeout"] and not chk["crash"], reasons,
                "check %s (rc=%s)" %
                ("TIMEOUT" if chk["timeout"] else "CRASH", chk["rc"]))
        require(chk["rc"] == case.compile_rc, reasons,
                "check rc=%s != expected %s" % (chk["rc"], case.compile_rc))
        for sub in case.compile_diag:
            require(sub in cdiag, reasons,
                    "check missing exact diagnostic %r" % sub)
        if case.outcome == "runtime_trap":
            require((OK_LINE_PREFIX.replace("<SRC>", str(src)) in cdiag)
                    or ("check: " in cdiag and " ok" in cdiag), reasons,
                    "check missing acceptance line for runtime_trap case")
        require(not sanitizer_markers(cdiag), reasons,
                "check has sanitizer markers %r" % (sanitizer_markers(cdiag),))

        # Step 2: c+ build with sanitizers disabled.
        build_argv = [BIN, "build", *CPLUS_FLAGS, str(src), "-o", str(binary)]
    else:
        # Rust: rustc is both the check and the build.  A compile-time reject
        # is asserted by E-code + message; a runtime trap must compile first.
        build_argv = [RUSTC, *RUST_FLAGS, str(src), "-o", str(binary)]

    bld = run_cmd(build_argv, env)
    bdiag = normalise(bld["out"] + bld["err"], src)
    write_log(case.name, "compile", build_argv, bld, bdiag)
    record["steps"]["compile"] = step_record(build_argv, bld, bdiag)
    require(not bld["timeout"] and not bld["crash"], reasons,
            "compile %s (rc=%s)" %
            ("TIMEOUT" if bld["timeout"] else "CRASH", bld["rc"]))
    require(bld["rc"] == case.compile_rc, reasons,
            "compile rc=%s != expected %s" % (bld["rc"], case.compile_rc))
    for sub in case.compile_diag:
        require(sub in bdiag, reasons,
                "compile missing exact diagnostic %r" % sub)
    require(not sanitizer_markers(bdiag), reasons,
            "compile has sanitizer markers %r" % (sanitizer_markers(bdiag),))

    if case.outcome == "compile_reject":
        require(not binary.exists(), reasons,
                "compile_reject case unexpectedly produced a binary")
        record["result"] = "COMPILE_REJECT"
        record["passed"] = not reasons
        return record

    # runtime_trap: a binary must exist, then be executed.
    require(binary.exists(), reasons, "runtime_trap case produced no binary")
    if not binary.exists():
        record["result"] = "NO_BINARY"
        record["passed"] = False
        return record

    run_argv = [binary]
    runr = run_cmd(run_argv, env)
    rout = normalise(runr["out"] + runr["err"], src)
    write_log(case.name, "run", run_argv, runr, rout)
    record["steps"]["run"] = step_record(run_argv, runr, rout)
    require(not runr["timeout"] and not runr["crash"], reasons,
            "run %s (rc=%s)" %
            ("TIMEOUT" if runr["timeout"] else "CRASH", runr["rc"]))
    require(runr["rc"] == case.exit_code, reasons,
            "run rc=%s != exact expected %s" % (runr["rc"], case.exit_code))
    for sub in case.run_diag:
        require(sub in rout, reasons,
                "run missing exact diagnostic %r" % sub)
    require(not sanitizer_markers(rout), reasons,
            "run has sanitizer markers %r" % (sanitizer_markers(rout),))
    record["result"] = "RUNTIME_TRAP"
    record["passed"] = not reasons
    return record


# ---------------------------------------------------------------------------
# reporting
# ---------------------------------------------------------------------------

def versions():
    out = {}
    res = run_cmd([BIN, "--version"], env=cplus_env(), timeout=15)
    out["cplus"] = (res["out"] + res["err"]).strip().splitlines()
    res = run_cmd([RUSTC, "--version"], env=cplus_env(), timeout=15)
    out["rustc"] = (res["out"] + res["err"]).strip().splitlines()
    return out


def write_summary(records, vers):
    failed = [r for r in records if not r["passed"]]
    failures = [{"case": r["name"], "category": r["category"],
                 "lang": r["lang"], "outcome": r["outcome"],
                 "reasons": r["reasons"]} for r in failed]

    by_category = {}
    for cat in CATEGORIES:
        cr = [r for r in records if r["category"] == cat]
        by_category[cat] = {
            "cases": len(cr),
            "passed": sum(1 for r in cr if r["passed"]),
            "failed": sum(1 for r in cr if not r["passed"]),
            "cplus": next((r["outcome"] for r in cr if r["lang"] == "cplus"), "?"),
            "rust": next((r["outcome"] for r in cr if r["lang"] == "rust"), "?"),
        }

    by_outcome = dict(Counter(r["outcome"] for r in records))
    by_lang_outcome = {}
    for lang in ("cplus", "rust"):
        lr = [r for r in records if r["lang"] == lang]
        by_lang_outcome[lang] = dict(Counter(r["outcome"] for r in lr))

    payload = {
        "suite": "memory_safety_compare",
        "generated_at": datetime.datetime.now().isoformat(timespec="seconds"),
        "toolchains": {"cplus_bin": BIN, "cplus_versions": vers["cplus"],
                       "rustc": RUSTC, "rustc_versions": vers["rustc"]},
        "flags": {
            "cplus_check": " ".join([BIN, "check", *CPLUS_FLAGS, "<SRC>"]),
            "cplus_build": " ".join([BIN, "build", *CPLUS_FLAGS,
                                     "<SRC>", "-o", "<BIN>"]),
            "rustc": " ".join([RUSTC, *RUST_FLAGS, "<SRC>", "-o", "<BIN>"]),
            "sanitizers": "DISABLED (c+ --sanitize=none; rustc without -Zsanitizer)",
        },
        "timeout_seconds": TIMEOUT,
        "workers": WORKERS,
        "totals": {
            "categories": len(CATEGORIES),
            "cases": len(records),
            "cases_passed": len(records) - len(failed),
            "cases_failed": len(failed),
            "by_outcome": by_outcome,
            "by_lang_outcome": by_lang_outcome,
        },
        "by_category": by_category,
        "failures": failures,
        "cases": records,
    }
    (RESULTS / "summary.json").write_text(json.dumps(payload, indent=2) + "\n")

    t = payload["totals"]
    lines = []
    add = lines.append
    add("C+ vs Rust memory-safety violation comparison")
    add("=" * 72)
    add("c+ binary   : %s" % BIN)
    add("c+ version  : %s" % (vers["cplus"][0] if vers["cplus"] else "(unknown)"))
    add("rustc       : %s" % RUSTC)
    add("rustc ver   : %s" % (vers["rustc"][0] if vers["rustc"] else "(unknown)"))
    add("generated   : %s" % payload["generated_at"])
    add("c+ flags    : %s" % payload["flags"]["cplus_check"])
    add("              %s" % payload["flags"]["cplus_build"])
    add("rustc flags : %s" % payload["flags"]["rustc"])
    add("sanitizers  : %s" % payload["flags"]["sanitizers"])
    add("timeout     : %ss per subprocess" % TIMEOUT)
    add("")
    add("SUMMARY")
    add("-" * 72)
    add("categories        : %d" % t["categories"])
    add("cases             : %d (1 C+ + 1 Rust per category)" % t["cases"])
    add("cases passed      : %d" % t["cases_passed"])
    add("cases failed      : %d" % t["cases_failed"])
    add("by outcome        : %s" % ", ".join(
        "%s=%d" % (k, t["by_outcome"][k]) for k in sorted(t["by_outcome"])))
    for lang in ("cplus", "rust"):
        d = t["by_lang_outcome"][lang]
        add("  %-14s : %s" % (lang, ", ".join("%s=%d" % (k, d[k])
                                              for k in sorted(d))))
    add("")
    add("PER CATEGORY (stage at which each language rejects)")
    add("-" * 72)
    add("  %-24s %-16s %-16s %s" %
        ("category", "C+", "Rust", "result"))
    for cat in CATEGORIES:
        info = by_category[cat]
        ok = "PASS" if info["failed"] == 0 else "FAIL(%d)" % info["failed"]
        add("  %-24s %-16s %-16s %s" %
            (cat, info["cplus"], info["rust"], ok))
    add("")
    add("FAILURES (exact case names and reasons)")
    add("-" * 72)
    if not failures:
        add("  none")
    else:
        for f in failures:
            add("  %s [%s / %s / %s]" %
                (f["case"], f["lang"], f["category"], f["outcome"]))
            for reason in f["reasons"]:
                add("      - %s" % reason)
    add("")
    add("Evidence: results/summary.json ; exact commands/outputs: results/logs/")
    (RESULTS / "summary.txt").write_text("\n".join(lines) + "\n")
    return payload


def main():
    if not pathlib.Path(BIN).exists():
        print("error: CPLUS_BIN not found: %s" % BIN, file=sys.stderr)
        return 2

    names = [c.name for c in CASES]
    if len(names) != len(set(names)):
        raise SystemExit("duplicate case name in manifest")
    if {c.category for c in CASES} != set(CATEGORIES):
        raise SystemExit("category manifest mismatch")
    for cat in CATEGORIES:
        pair = [c for c in CASES if c.category == cat]
        if sorted(c.lang for c in pair) != ["cplus", "rust"]:
            raise SystemExit("category %s is not a C+/Rust pair" % cat)

    CASES_DIR.mkdir(parents=True, exist_ok=True)
    LOGS.mkdir(parents=True, exist_ok=True)

    print("memory_safety_compare: %d categories, %d cases, sanitizers disabled"
          % (len(CATEGORIES), len(CASES)))
    print("c+ : %s" % BIN)
    print("rustc: %s" % RUSTC)

    records = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for rec in pool.map(evaluate, CASES):
            records.append(rec)

    payload = write_summary(records, versions())
    t = payload["totals"]
    print("cases: %d passed / %d failed" % (t["cases_passed"], t["cases_failed"]))
    print("outcomes: %s" % ", ".join("%s=%d" % (k, t["by_outcome"][k])
                                     for k in sorted(t["by_outcome"])))
    for f in payload["failures"]:
        print("  - %s [%s / %s]" % (f["case"], f["lang"], f["outcome"]))
        for reason in f["reasons"]:
            print("      - %s" % reason)
    print("summary: %s" % (RESULTS / "summary.txt"))
    return 1 if payload["failures"] else 0


if __name__ == "__main__":
    sys.exit(main())
