#!/usr/bin/env python3
"""Behavioral regressions for GitHub issue #235 (STRICT gate bypasses).

Each case runs `c+ check --strict --sanitize=none` on the same clean fixture;
only the pass-through flags after `--` differ. Exit status and diagnostic
substrings are asserted exactly. Sanitizer probes are disabled so results do
not depend on a host sanitizer runtime.

  CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/driver_issue_235/run_suite.py

No skips, no `|| true`; every process has a hard timeout and a timeout is a
FAIL. The companion source_guard.py needs no built compiler.
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
FIXTURE = os.path.join(HERE, "fixture.c")
TIMEOUT = int(os.environ.get("CPLUS_ISSUE235_TIMEOUT", "60"))

ALLOWLIST = "pass-through flag outside the STRICT Safe allow-list"
PARSE_GATE = "change how the C+ safety AST parses"
OK = "check: " + FIXTURE + " ok"

CASES = [
    # Control: no pass-through flags, STRICT check must succeed.
    dict(name="control_clean", flags=[], rc=0, require=[OK]),
    # Legitimate single-dash -l must keep working (guards against an
    # over-broad rejection of the raw short-option branch).
    dict(name="control_single_dash_l", flags=["-lm"], rc=0, require=[OK]),
    # --ld-path=... used to be normalized to -ld-path=... and pass as -l.
    dict(name="ld_path_joined", flags=["--ld-path=/path/to/evil-ld"], rc=1,
         require=[ALLOWLIST]),
    dict(name="ld_path_two_token", flags=["--ld-path", "/path/to/evil-ld"],
         rc=1, require=[ALLOWLIST]),
    dict(name="uppercase_L_long", flags=["--L/path/to/evil"], rc=1,
         require=[ALLOWLIST]),
    # --language=... used to pass as -l and made clang compile C++ while the
    # safety AST saw C. Both spellings must hit the parse-divergence gate.
    dict(name="language_joined", flags=["--language=c++"], rc=1,
         require=[PARSE_GATE, "--language"]),
    dict(name="language_two_token", flags=["--language", "c++"], rc=1,
         require=[PARSE_GATE, "--language"]),
    # Existing -x spelling stays rejected (no regression from the shared
    # isLanguageOption() helper).
    dict(name="x_joined", flags=["-xc++"], rc=1, require=[PARSE_GATE]),
    # --std= is a clang long option whose single-dash spelling is forwarded to
    # the AST; it must not slip through the old normalized -std= check.
    dict(name="std_long", flags=["--std=c++17"], rc=1, require=[ALLOWLIST]),
]


def run_case(case):
    cmd = [BIN, "check", "--strict", "--sanitize=none", FIXTURE]
    if case["flags"]:
        cmd += ["--"] + case["flags"]
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
    rec["missing"] = missing
    rec["ok"] = (not rec["timed_out"] and rec["rc"] == case["rc"] and
                 not missing)
    return rec


def main():
    if not os.path.isfile(BIN):
        print("FAIL: c+ binary not found at %s (set CPLUS_BIN)" % BIN)
        return 2
    if not os.path.isfile(FIXTURE):
        print("FAIL: fixture missing at %s" % FIXTURE)
        return 2

    failed = 0
    for case in CASES:
        rec = run_case(case)
        status = "PASS" if rec["ok"] else "FAIL"
        if not rec["ok"]:
            failed += 1
        print("%s %-22s rc=%s expected=%s missing=%s" %
              (status, rec["name"], rec["rc"],
               case["rc"], rec["missing"]))
        if not rec["ok"]:
            print("  command: %s" % " ".join(rec["command"]))
            print("  output: %s" % rec["out"].strip()[:800])

    print("issue #235 CLI suite: %d/%d passed" % (len(CASES) - failed,
                                                   len(CASES)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
