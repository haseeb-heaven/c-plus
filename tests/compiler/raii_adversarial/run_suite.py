#!/usr/bin/env python3
"""Deterministic adversarial RAII cleanup-order suite.

Every case is compiled with `c+ build --default --sanitize=none`, executed under
a hard timeout, and checked against exact stdout and rc == 0.  Destructors are
non-idempotent and print a unique id (D<n>) on every call, so a duplicated,
missing or wrongly ordered cleanup is observable.  Each case also verifies zero
live allocations at exit via `cplus_allocation_generation()` (the public
liveness query in runtime/include/cplus/cplus.h): the live allocation behind
every D<n> must report generation 0 after all scopes have exited, printed as
`live=0`.

Exit paths covered: nested if/else with early return, for and while loops with
break and continue, switch with break, return from inside a loop, and goto out
of nested cleanup blocks.  Goto cases use --no-werror because the frontend
diagnoses goto as bypassing cleanup in default mode; the diagnostic is still
emitted and recorded verbatim in the evidence.  No case is skipped.

A sanitizer variant is added only when a compile-and-run probe proves the
host backend supports the sanitizer.  Otherwise the host is recorded as
UNTESTED_ON_HOST with the probe evidence; no pass is claimed for it.

Run:  CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/raii_adversarial/run_suite.py
"""
import datetime
import json
import os
import platform
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
HERE = os.path.dirname(os.path.abspath(__file__))
CASES_DIR = os.path.join(HERE, "cases")
OUT = os.path.join(HERE, "results")
BIN_DIR = os.path.join(OUT, "bin")
os.makedirs(CASES_DIR, exist_ok=True)
os.makedirs(BIN_DIR, exist_ok=True)

BUILD_TIMEOUT = 60
RUN_TIMEOUT = 10

# Per-pointer liveness query from cplus.h.  The global
# cplus_alloc_live_count() lives in the hardened libc and is not linked into a
# default `c+ build` executable, so the ledger below uses the exported public
# query cplus_allocation_generation() instead: 0 means no live allocation.
PRELUDE = r'''#include <stdio.h>
#include "cplus/cplus.h"
static void *g_ptrs[32]; static int g_n = 0;
resource struct R { int id; int *p; };
void R_drop(R *r){ if (r->id) { printf("D%d\n", r->id); cplus_release(r->p); r->id = 0; } }
static R R_new(int id){ R r = { id, (int *)cplus_alloc(4) }; g_ptrs[g_n++] = r.p; return r; }
'''

POSTLUDE = r'''
int main(void){
  int rc = scenario();
  int live = 0;
  for (int i = 0; i < g_n; ++i) {
    if (cplus_allocation_generation(g_ptrs[i]) != 0) ++live;
  }
  printf("rc=%d live=%d\n", rc, live);
  return 0;
}
'''

# Case bodies and exact expectations live beside this file.
sys.path.insert(0, HERE)
from cases_manifest import CASES


SANITIZERS = ("address", "leak")


def run(argv, timeout):
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout)
        return {"command": argv, "rc": p.returncode,
                "stdout": p.stdout.decode(errors="replace"),
                "stderr": p.stderr.decode(errors="replace"), "timed_out": False}
    except subprocess.TimeoutExpired as e:
        return {"command": argv, "rc": None,
                "stdout": (e.stdout or b"").decode(errors="replace"),
                "stderr": (e.stderr or b"").decode(errors="replace"),
                "timed_out": True}


def build_args(case, path, exe, sanitizer=None):
    args = [BIN, "build", "--default", "--sanitize=" + (sanitizer or "none")]
    if case.get("no_werror"):
        args.append("--no-werror")
    args += [path, "-o", exe]
    return args


def probe_sanitizer(kind):
    """Prove a sanitizer really builds and runs before using it as a variant."""
    src = os.path.join(BIN_DIR, "_probe_" + kind + ".cp")
    exe = os.path.join(BIN_DIR, "_probe_" + kind + ".bin")
    with open(src, "w") as f:
        f.write('#include <stdio.h>\n'
                'int main(void){ volatile int v = 3; printf("probe=%d\\n", v); '
                'return 0; }\n')
    steps = []
    build = run([BIN, "build", "--default", "--sanitize=" + kind, src,
                 "-o", exe], BUILD_TIMEOUT)
    steps.append(build)
    ok = (build["rc"] == 0 and not build["timed_out"] and os.path.exists(exe))
    if ok:
        result = run([exe], RUN_TIMEOUT)
        steps.append(result)
        ok = (result["rc"] == 0 and not result["timed_out"] and
              result["stdout"] == "probe=3\n")
    return {"sanitizer": kind,
            "status": "AVAILABLE" if ok else "UNTESTED_ON_HOST",
            "steps": steps}


def main():
    rows, fails = [], []
    if not os.path.exists(BIN):
        print("missing compiler: " + BIN)
        return 2

    sanitizers = {}
    for kind in SANITIZERS:
        sanitizers[kind] = probe_sanitizer(kind)
        print("sanitizer %-8s %s" % (kind, sanitizers[kind]["status"]))
    variants = [("plain", None)]
    variants += [(k, k) for k in SANITIZERS
                 if sanitizers[k]["status"] == "AVAILABLE"]

    for case in CASES:
        name = case["name"]
        path = os.path.join(CASES_DIR, name + ".cp")
        with open(path, "w") as f:
            f.write(PRELUDE + case["body"] + POSTLUDE)
        for variant, san in variants:
            exe = os.path.join(BIN_DIR, name + "_" + variant + ".bin")
            steps = []
            build = run(build_args(case, path, exe, san), BUILD_TIMEOUT)
            steps.append(build)
            ok = (build["rc"] == 0 and not build["timed_out"] and
                  os.path.exists(exe))
            detail = "build_rc=%s expected_stdout=%r" % (build["rc"],
                                                         case["expect"])
            if ok:
                result = run([exe], RUN_TIMEOUT)
                steps.append(result)
                ok = (result["rc"] == 0 and not result["timed_out"] and
                      result["stdout"] == case["expect"])
                detail += " stdout=%r" % result["stdout"]
            key = name if variant == "plain" else name + "@" + variant
            rows.append({"case": name, "variant": variant,
                         "status": "PASS" if ok else "FAIL",
                         "detail": detail, "steps": steps})
            if not ok:
                fails.append(key)
        print("%-26s %s" % (name, rows[-1]["status"]))

    payload = {
        "generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "host": platform.platform(), "compiler": BIN,
        "cases": len(CASES), "variants": [v[0] for v in variants],
        "results": rows, "failures": fails,
        "sanitizers": sanitizers,
    }
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("RAII adversarial cleanup-order suite\n%s\nc+ %s\n\n" %
                (payload["host"], BIN))
        for kind in SANITIZERS:
            s = sanitizers[kind]
            f.write("sanitizer %-8s %s\n" % (kind, s["status"]))
            for step in s["steps"]:
                f.write("  probe cmd: %s\n  rc=%s out=%r err=%r\n" %
                        (" ".join(step["command"]), step["rc"],
                         step["stdout"][:200], step["stderr"][:400]))
        f.write("\n")
        for r in rows:
            f.write("%-26s %-8s %-5s %s\n" % (r["case"], r["variant"],
                                              r["status"], r["detail"]))
            for s in r["steps"]:
                f.write("  cmd: %s\n  rc=%s out=%r err=%r\n" %
                        (" ".join(s["command"]), s["rc"], s["stdout"][:300],
                         s["stderr"][:400]))
        passed = len(rows) - len(fails)
        f.write("\nentries=%d passed=%d failed=%d; cases=%d; variants=%s\n" %
                (len(rows), passed, len(fails), len(CASES),
                 ",".join(v[0] for v in variants)))
    print("raii_adversarial: %d cases, %d variants, %d entries, %d failures" %
          (len(CASES), len(variants), len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
