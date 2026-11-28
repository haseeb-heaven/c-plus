#!/usr/bin/env python3
"""Behavioral regressions for GitHub issue #245: dangling -I/-D/-U/-L/-l.

A bare -I/-D/-U/-L/-l with no following token used to print an error but
keep parsing, reading argv[argc] (NULL) and dying with SIGSEGV. Each case
below asserts an exact exit status of 1 and the exact diagnostic, so a
signal death (negative returncode in Python) is a FAIL, never a pass.

  CPLUS_BIN=$PWD/build-issue245/c+ python3 tests/compiler/driver_issue_245/run_suite.py

No skips; every process has a hard timeout and a timeout is a FAIL. Needs a
built c+ but no clang and no sanitizer runtime.
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
TIMEOUT = int(os.environ.get("CPLUS_ISSUE245_TIMEOUT", "30"))

CRASH_MARKERS = ["Stack dump", "PLEASE submit a bug report", "Segmentation"]

CASES = [dict(name="control_help", args=["help"], rc=0,
              require=["usage:", "c+"], forbid=[])]
for flag in ("-I", "-D", "-U", "-L", "-l"):
    CASES.append(dict(name="dangling_%s" % flag[1], args=[flag], rc=1,
                      require=["error: %s requires a value" % flag],
                      forbid=CRASH_MARKERS))
    CASES.append(dict(name="dangling_build_%s" % flag[1],
                      args=["build", flag], rc=1,
                      require=["error: %s requires a value" % flag],
                      forbid=CRASH_MARKERS))


def run_case(case):
    cmd = [BIN] + case["args"]
    started = time.time()
    rec = {"name": case["name"], "command": cmd, "out": "", "timed_out": False}
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=TIMEOUT, cwd=ROOT)
        rec["rc"] = p.returncode
        rec["out"] = (p.stdout + p.stderr).decode("utf-8", errors="replace")
    except subprocess.TimeoutExpired as exc:
        rec["rc"] = "TIMEOUT"
        rec["timed_out"] = True
        rec["out"] = ((exc.stdout or b"") + (exc.stderr or b"")).decode(
            "utf-8", errors="replace")
    rec["seconds"] = round(time.time() - started, 3)
    missing = [s for s in case["require"] if s not in rec["out"]]
    present = [s for s in case["forbid"] if s in rec["out"]]
    rec["missing"] = missing
    rec["forbid"] = present
    # A negative returncode (signal, e.g. -11 for SIGSEGV) cannot equal 1.
    rec["ok"] = (not rec["timed_out"] and rec["rc"] == case["rc"] and
                 not missing and not present)
    return rec


def main():
    if not os.path.isfile(BIN):
        print("FAIL: c+ binary not found at %s (set CPLUS_BIN)" % BIN)
        return 2

    failed = 0
    for case in CASES:
        rec = run_case(case)
        status = "PASS" if rec["ok"] else "FAIL"
        if not rec["ok"]:
            failed += 1
        print("%s %-22s rc=%s expected=%s missing=%s forbidden=%s" %
              (status, rec["name"], rec["rc"], case["rc"],
               rec["missing"], rec["forbid"]))
        if not rec["ok"]:
            print("  command: %s" % " ".join(rec["command"]))
            print("  output: %s" % rec["out"].strip()[:800])

    print("issue #245 CLI suite: %d/%d passed" % (len(CASES) - failed,
                                                   len(CASES)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
