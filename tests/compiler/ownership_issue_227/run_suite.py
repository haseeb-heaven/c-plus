#!/usr/bin/env python3
"""Deterministic regressions for compiler issue #227.

Issue #227 has two independent bypasses, both observed here through the exact
APIs named in the report:

* ``hasPointerEffectSignature`` (reached via ``cplus::probeSafetyAst``) used to
  return false for bodiless externs whose pointer is nested in a by-value
  struct/union, for variadic externs and for unprototyped K&R externs. Those
  calls escaped STRICT's ``unverifiedPointerExterns`` gate.
* ``findUserDeclaredHelperSymbols`` used to accept a reserved runtime guard
  whenever its declaration lived outside the main file, so a local
  ``#include "evil.h"`` could silently interpose a safety guard.

The probe is compiled from a FRESHLY built ``compiler/Analysis/OwnershipAnalysis.cpp``
with the include/define flags from ``build/compile_commands.json`` and linked
ahead of ``libcplus_core.a`` (same technique as
``tests/compiler/ownership_issues/run_suite.py``), so a stale object inside the
archive cannot mask either regression. No case is skipped and no failure is
suppressed.

Run (after the parent session has configured/built the tree):

    CPLUS_BUILD_DIR=$PWD/build python3 tests/compiler/ownership_issue_227/run_suite.py

End-to-end STRICT CLI checks can then be exercised with a rebuilt ``c+``:

    build/c+ check --strict --sanitize=none tests/compiler/ownership_issue_227/cases/ffi_neg_struct_by_value.c
    build/c+ check --strict --sanitize=none tests/compiler/ownership_issue_227/cases/helper_neg_guard_in_header.c

Both must fail with ``[CPLUS-FFI]``; the ``ffi_pos_*`` / ``helper_pos_*``
fixtures must succeed.
"""
import datetime
import json
import os
import platform
import shlex
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
BUILD_DIR = os.path.abspath(
    os.environ.get("CPLUS_BUILD_DIR", os.path.join(ROOT, "build")))
CASES_DIR = os.path.join(HERE, "cases")
OUT = os.path.join(HERE, "results")
os.makedirs(CASES_DIR, exist_ok=True)
os.makedirs(OUT, exist_ok=True)

COMPILE_TIMEOUT = 120
LINK_TIMEOUT = 300
PROBE_TIMEOUT = 90

# (fixture, expected unverified, expected headerDeclared,
#  expected unresolved, expected reserved helpers).  `parsed` must be 1 for
# every case. Exact list equality -- no subset/substring checks.
CASES = [
    ("ffi_neg_struct_by_value", ["evil_struct"], [], ["evil_struct"], []),
    ("ffi_neg_struct_return", ["evil_return"], [], ["evil_return"], []),
    ("ffi_neg_nested_struct", ["evil_nested"], [], ["evil_nested"], []),
    ("ffi_neg_typedef_struct", ["evil_typedef"], [], ["evil_typedef"], []),
    ("ffi_neg_array_in_struct", ["evil_array"], [], ["evil_array"], []),
    ("ffi_neg_union", ["evil_union"], [], ["evil_union"], []),
    ("ffi_neg_variadic", ["evil_variadic"], [], ["evil_variadic"], []),
    ("ffi_neg_unprototyped", ["evil_kr"], [], ["evil_kr"], []),
    ("ffi_pos_scalar_only", [], [], ["evil_scalar"], []),
    ("ffi_pos_struct_no_pointer", [], [], ["evil_pair"], []),
    ("ffi_pos_direct_pointer", ["evil_ptr"], [], ["evil_ptr"], []),
    ("ffi_header_declared_pointer", ["header_ffi"], ["header_ffi"],
     ["header_ffi"], []),
    ("ffi_pos_libc_system_header", [], [], [], []),
    ("helper_neg_guard_in_header", [], [], [], ["__cplus_checked_add_i64"]),
    ("helper_neg_bounds_check_in_header", [], [], [], ["__cplus_bounds_check"]),
    ("helper_neg_var_in_header", [], [], [], ["__cplus_inline_state"]),
    ("helper_pos_main_file", [], [], [], ["__cplus_checked_mul_i64"]),
    ("helper_pos_user_helper_in_header", [], [], [], []),
    ("helper_pos_no_helpers", [], [], [], []),
]


def run(argv, timeout, env=None, cwd=None):
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout, env=env, cwd=cwd)
        return {"command": argv, "rc": p.returncode,
                "stdout": p.stdout.decode(errors="replace"),
                "stderr": p.stderr.decode(errors="replace"), "timed_out": False}
    except subprocess.TimeoutExpired as e:
        return {"command": argv, "rc": None,
                "stdout": (e.stdout or b"").decode(errors="replace"),
                "stderr": (e.stderr or b"").decode(errors="replace"),
                "timed_out": True}


def record(rows, name, ok, detail, steps):
    rows.append({"case": name, "status": "PASS" if ok else "FAIL",
                 "detail": detail, "steps": steps})
    print("%-38s %s" % (name, "ok  " if ok else "FAIL"))
    return ok


def build_probe(rows):
    """Compile OwnershipAnalysis.cpp + probe and link ahead of the archive."""
    db_path = os.path.join(BUILD_DIR, "compile_commands.json")
    link_path = os.path.join(BUILD_DIR, "CMakeFiles", "c+.dir", "link.txt")
    core_a = os.path.join(BUILD_DIR, "libcplus_core.a")
    for path in (db_path, link_path, core_a):
        if not os.path.exists(path):
            print("missing build artifact: %s (configure/build the tree first)"
                  % path, file=sys.stderr)
            return None
    with open(db_path) as f:
        db = json.load(f)
    entries = [e for e in db
               if os.path.basename(e.get("file", "")).endswith(
                   "OwnershipAnalysis.cpp")]
    if len(entries) != 1:
        print("expected exactly one OwnershipAnalysis.cpp compile command, "
              "found %d" % len(entries), file=sys.stderr)
        return None
    cmd = shlex.split(entries[0]["command"])
    out_idx = max(i for i, t in enumerate(cmd) if t == "-o")
    compiler, flags = cmd[0], cmd[1:out_idx]

    oa_obj = os.path.join(OUT, "OwnershipAnalysis.issue227.o")
    probe_obj = os.path.join(OUT, "ownership_issue_227_probe.o")
    probe_exe = os.path.join(OUT, "ownership_issue_227_probe")
    oa_src = os.path.join(ROOT, "compiler", "Analysis", "OwnershipAnalysis.cpp")
    probe_src = os.path.join(HERE, "ownership_issue_227_probe.cpp")
    for label, src, obj in (("compile_ownership_analysis", oa_src, oa_obj),
                            ("compile_probe", probe_src, probe_obj)):
        step = run([compiler] + flags + ["-o", obj, "-c", src], COMPILE_TIMEOUT)
        ok = step["rc"] == 0 and not step["timed_out"]
        record(rows, label, ok, "rc=%s" % step["rc"], [step])
        if not ok:
            return None

    with open(link_path) as f:
        link = shlex.split(f.read())
    link = [t for t in link if not t.endswith("Main.cpp.o")]
    out_idx = link.index("-o")
    link[out_idx + 1] = probe_exe
    link = [link[0], oa_obj, probe_obj] + link[1:]
    step = run(link, LINK_TIMEOUT, cwd=BUILD_DIR)
    ok = step["rc"] == 0 and not step["timed_out"]
    record(rows, "link_probe", ok, "rc=%s" % step["rc"], [step])
    if not ok:
        return None
    return probe_exe


def parse_output(text):
    fields = {}
    for line in text.splitlines():
        key, sep, value = line.partition("=")
        if sep and key in ("parsed", "unverified", "header", "unresolved",
                           "helper"):
            fields.setdefault(key, []).append(value)
    return fields


def main():
    rows = []
    with tempfile.TemporaryDirectory(prefix="cplus_issue227_") as td:
        contract = os.path.join(td, "empty_contracts.json")
        with open(contract, "w") as f:
            json.dump({"version": 2, "functions": []}, f)
        probe = build_probe(rows)
        if probe:
            env = dict(os.environ, CPLUS_CONTRACT_PATH=contract)
            for name, unverified, header, unresolved, helper in CASES:
                fixture = os.path.join(CASES_DIR, name + ".c")
                if not os.path.exists(fixture):
                    record(rows, name, False, "missing fixture %s" % fixture, [])
                    continue
                step = run([probe, fixture], PROBE_TIMEOUT, env=env, cwd=HERE)
                got = parse_output(step["stdout"])
                expected = {"parsed": ["1"], "unverified": unverified,
                            "header": header, "unresolved": unresolved,
                            "helper": helper}
                mismatches = []
                for key in expected:
                    actual = got.get(key, [])
                    if actual != expected[key]:
                        mismatches.append("%s: expected %s got %s" %
                                          (key, expected[key], actual))
                ok = (step["rc"] == 0 and not step["timed_out"] and
                      not mismatches)
                record(rows, name, ok,
                       "rc=%s %s" % (step["rc"], "; ".join(mismatches)),
                       [step])

    failures = [r["case"] for r in rows if r["status"] == "FAIL"]
    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "host": platform.platform(), "build_dir": BUILD_DIR,
               "results": rows, "failures": failures}
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("Ownership issue #227 regression suite\n")
        for r in rows:
            f.write("%-38s %s  %s\n" % (r["case"], r["status"], r["detail"]))
            for s in r["steps"]:
                f.write("  cmd: %s\n  rc=%s out=%r err=%r\n" %
                        (" ".join(s["command"]), s["rc"], s["stdout"][:1000],
                         s["stderr"][:1000]))
        f.write("\ncases=%d passed=%d failed=%d\n" %
                (len(rows), len(rows) - len(failures), len(failures)))
    print("ownership_issue_227: %d cases, %d failures" %
          (len(rows), len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
