#!/usr/bin/env python3
"""Reproduce the KNOWN_OPEN cross-TU ownership-declaration mismatch (issue #4
residual gap / docs/LIMITATIONS.md Bug 35).

This is NOT a passing safety test: it is evidence that the *documented* gap
still exists.  Two real translation units are compiled with the PREBUILT C+
compiler (no rebuild), linked with the reference C compiler, and executed.
TU A defines `void resource_release(int *)`; TU B declares the same symbol as
`void resource_release(long *)`.  A single-TU compiler cannot compare them, so
every step is expected to succeed with no mismatch diagnostic.

Usage:
    python3 tests/known_gaps/abi_mismatch/run.py

Environment (optional):
    CPLUS_BIN   path to the prebuilt c+ binary   (default <root>/build/c+)
    REF_CC      path to a C compiler/linker      (default /usr/bin/clang)
"""
from __future__ import annotations

import datetime
import json
import os
import pathlib
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
RESULTS = HERE / "results"
RESULTS.mkdir(parents=True, exist_ok=True)

CPLUS_BIN = os.environ.get("CPLUS_BIN", str(ROOT / "build" / "c+"))
REF_CC = os.environ.get("REF_CC", "/usr/bin/clang")
RUNTIME_LIB = ROOT / "build" / "libcplus_runtime.a"
MODES = ("--legacy-c", "--default", "--strict")
SANITIZE = "--sanitize=none"
TIMEOUT = 90

DEF_SRC = HERE / "cross_tu_owner_def.c"      # TU A: void resource_release(int *)
DECL_SRC = HERE / "cross_tu_owner_decl.c"    # TU B: void resource_release(long *)
SENTINEL = "no cross-TU diagnostic"


def run(argv):
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=TIMEOUT)
        return {"command": argv, "rc": p.returncode,
                "stdout": p.stdout.decode(errors="replace"),
                "stderr": p.stderr.decode(errors="replace"), "timed_out": False}
    except subprocess.TimeoutExpired as e:
        return {"command": argv, "rc": None,
                "stdout": (e.stdout or b"").decode(errors="replace"),
                "stderr": (e.stderr or b"").decode(errors="replace"),
                "timed_out": True}


def has_mismatch_diag(text):
    # Only real diagnostics count.  A bare substring search would false-positive
    # on the literal path component "abi_mismatch" echoed in every source line,
    # and on the permitted STRICT [CPLUS-MULTITU] "note:" disclosure.
    for line in text.splitlines():
        low = line.lower()
        if ("error:" in low or "warning:" in low) and (
                "mismatch" in low or "incompatible" in low or
                "conflicting types" in low):
            return True
    return False


def main():
    problems = []
    for path in (CPLUS_BIN, REF_CC, str(RUNTIME_LIB), str(DEF_SRC), str(DECL_SRC)):
        if not pathlib.Path(path).exists():
            problems.append("missing prerequisite: " + path)
    if problems:
        for p in problems:
            print("ERROR:", p, file=sys.stderr)
        return 2

    # Fixed workdir => the commands recorded in results/summary.txt are stable
    # and can be pasted verbatim into docs/compiler/abi_multi.md.
    work = RESULTS / "work"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    steps = []
    for mode in MODES:
        objs = []
        for idx, src in enumerate((DEF_SRC, DECL_SRC)):
            obj = work / ("%s.%s.o" % (src.stem, mode.lstrip("-")))
            rec = run([CPLUS_BIN, "build", str(src), mode, SANITIZE,
                       "-c", "-o", str(obj)])
            rec["step"] = "compile_%s_tu%d" % (mode.lstrip("-"), idx)
            steps.append(rec)
            objs.append(obj)
            if rec["rc"] != 0 or rec["timed_out"]:
                problems.append("%s: compile TU%d failed" % (rec["step"], idx))
            if has_mismatch_diag(rec["stdout"] + rec["stderr"]):
                problems.append("%s: unexpected mismatch diagnostic" % rec["step"])

        prog = work / ("prog.%s" % mode.lstrip("-"))
        rec = run([REF_CC, "-std=gnu11", "-O1"] + [str(o) for o in objs] +
                  [str(RUNTIME_LIB), "-o", str(prog)])
        rec["step"] = "link_%s" % mode.lstrip("-")
        steps.append(rec)
        if rec["rc"] != 0 or rec["timed_out"]:
            problems.append("%s: link failed" % rec["step"])
        if has_mismatch_diag(rec["stdout"] + rec["stderr"]):
            problems.append("%s: unexpected mismatch diagnostic" % rec["step"])

        if prog.exists():
            rec = run([str(prog)])
            rec["step"] = "run_%s" % mode.lstrip("-")
            steps.append(rec)
            if rec["rc"] != 0 or rec["stdout"].strip() != SENTINEL:
                problems.append("%s: rc=%s stdout=%r" %
                                (rec["step"], rec["rc"], rec["stdout"]))

    payload = {
        "purpose": "evidence that cross-TU incompatible ownership declarations "
                   "are NOT diagnosed (KNOWN_OPEN, issue #4 / Bug 35)",
        "generated_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "compiler": CPLUS_BIN, "ref_cc": REF_CC, "runtime_lib": str(RUNTIME_LIB),
        "tu_a": str(DEF_SRC), "tu_b": str(DECL_SRC),
        "workdir": str(work), "steps": steps, "problems": problems,
        "verdict": "KNOWN_OPEN" if not problems else "UNEXPECTED_DIAGNOSTIC",
    }
    with open(RESULTS / "summary.json", "w") as f:
        json.dump(payload, f, indent=2)
    with open(RESULTS / "summary.txt", "w") as f:
        f.write("KNOWN_OPEN reproduction: cross-TU incompatible ownership "
                "declarations are NOT diagnosed\n")
        f.write("TU A: void resource_release(int *)  (defines)\n")
        f.write("TU B: void resource_release(long *) (declares -> INCOMPATIBLE)\n")
        f.write("compiler: %s\nref cc: %s\n" % (CPLUS_BIN, REF_CC))
        f.write("Expectation: every step rc=0, no mismatch diagnostic, "
                "stdout=%r\n\n" % SENTINEL)
        for s in steps:
            f.write("$ %s\n" % " ".join(s["command"]))
            f.write("  rc=%s timed_out=%s\n" % (s["rc"], s["timed_out"]))
            if s["stdout"]:
                f.write("  stdout: %r\n" % s["stdout"])
            if s["stderr"]:
                for line in s["stderr"].splitlines():
                    f.write("  stderr: %s\n" % line)
        f.write("\nproblems: %d\n" % len(problems))
        for p in problems:
            f.write("  " + p + "\n")
        f.write("verdict: %s\n" % payload["verdict"])

    for s in steps:
        print("%-28s rc=%s %s" % (s["step"], s["rc"],
                                  s["stdout"].strip().replace("\n", " | ")))
    print("verdict: %s (%d unexpected results)" %
          (payload["verdict"], len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
