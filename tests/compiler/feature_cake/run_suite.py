#!/usr/bin/env python3
"""Deterministic regressions for issue #179 (Cake-style qualifiers + defer).

Run after the tree has been built:

    CPLUS_BUILD_DIR=$PWD/build python3 tests/compiler/feature_cake/run_suite.py

CHECK cases assert STRICT rejects/accepts the source with a specific
CPLUS-OPT / CPLUS-OWN / CPLUS-VIEW diagnostic. BUILD cases assert the emitted C
contains no Cake qualifier token, that sizeof(qualified pointer) equals
sizeof(void *), and that defer cleanup runs in deterministic LIFO order.
No case is skipped and no failure is suppressed.
"""
import datetime
import json
import os
import platform
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
BUILD_DIR = os.path.abspath(
    os.environ.get("CPLUS_BUILD_DIR", os.path.join(ROOT, "build")))
BIN = os.environ.get("CPLUS_BIN", os.path.join(BUILD_DIR, "c+"))
OUT = os.path.join(HERE, "results")
os.makedirs(OUT, exist_ok=True)

TIMEOUT = 90

# (name, kind, must_reject, required diagnostic fragment)
CHECK_CASES = [
    ("opt_unguarded_deref", "reject", True, "[CPLUS-OPT]"),
    ("opt_guarded_deref", "accept", False, ""),
    ("opt_guard_return", "accept", False, ""),
    ("opt_short_circuit", "accept", False, ""),
    ("opt_arrow_unguarded", "reject", True, "[CPLUS-OPT]"),
    ("opt_arrow_guarded", "accept", False, ""),
    ("owner_double_move", "reject", True, "[CPLUS-OWN]"),
    ("owner_use_after_move", "reject", True, "[CPLUS-OWN]"),
    ("owner_single_move_ok", "accept", False, ""),
    # Path/source-order sensitivity probes: one move per mutually exclusive
    # arm and a reassigned owner are safe; a move on a loop back edge is not.
    ("owner_branch_moves_ok", "accept", False, ""),
    ("owner_reassign_move_ok", "accept", False, ""),
    ("owner_loop_move_reject", "reject", True, "[CPLUS-OWN]"),
    ("owner_loop_reassign_move_ok", "accept", False, ""),
    ("view_move_reject", "reject", True, "[CPLUS-VIEW]"),
    ("view_borrow_ok", "accept", False, ""),
]

BUILD_CASES = ["abi_erasure", "defer_lifo", "defer_owner_exit"]


def run(argv, timeout=TIMEOUT):
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout)
        return {"command": " ".join(argv), "rc": p.returncode,
                "stdout": p.stdout.decode(errors="replace"),
                "stderr": p.stderr.decode(errors="replace"),
                "timed_out": False}
    except subprocess.TimeoutExpired as e:
        return {"command": " ".join(argv), "rc": None,
                "stdout": (e.stdout or b"").decode(errors="replace"),
                "stderr": (e.stderr or b"").decode(errors="replace"),
                "timed_out": True}


def record(rows, name, ok, detail, steps):
    rows.append({"case": name, "status": "PASS" if ok else "FAIL",
                 "detail": detail, "steps": steps})
    print("%-24s %s  %s" % (name, "PASS" if ok else "FAIL", detail))


def check_case(rows, name, kind, must_reject, fragment):
    src = os.path.join(HERE, name + ".c")
    step = run([BIN, "check", "--strict", "--sanitize=none", src])
    output = step["stdout"] + step["stderr"]
    ok = not step["timed_out"] and (
        step["rc"] != 0 if must_reject else step["rc"] == 0)
    if ok and fragment:
        ok = fragment in output
    record(rows, name, ok,
           "rc=%s fragment=%r" % (step["rc"], fragment), [step])


def build_case(rows, name):
    src = os.path.join(HERE, name + ".c")
    exe = os.path.join(OUT, name + ".bin")
    emitted = os.path.join(OUT, name + ".c")
    steps = []
    b = run([BIN, "build", "--strict", "--sanitize=none", "--emit-c", emitted,
             src, "-o", exe])
    steps.append(b)
    ok = (not b["timed_out"] and b["rc"] == 0 and os.path.exists(exe)
          and os.path.exists(emitted))
    if ok:
        text = open(emitted, "r", errors="replace").read()
        # Qualifiers must be fully erased before the backend sees the C.
        found = re.findall(r"\b(?:_Opt|_Owner|_View)\b", text)
        ok = not found
        r = run([exe])
        steps.append(r)
        ok = ok and not r["timed_out"] and r["rc"] == 0
        if ok and name == "abi_erasure":
            kv = dict(re.findall(r"(\w+)=(\d+)", r["stdout"]))
            ok = (kv.get("PTR") == kv.get("RAW") and kv.get("OFF") == "4")
        if ok and name == "defer_lifo":
            order = re.findall(r"D\d", r["stdout"])
            ok = order == ["D5", "D3", "D4", "D2", "D1"]
        if ok and name == "defer_owner_exit":
            ok = "CLEAN" in r["stdout"]
    record(rows, name, ok, "build/run/emit-c validated", steps)


def main():
    rows = []
    if not os.path.exists(BIN):
        print("missing c+ binary at %s" % BIN, file=sys.stderr)
        return 1
    for name, kind, must_reject, fragment in CHECK_CASES:
        check_case(rows, name, kind, must_reject, fragment)
    for name in BUILD_CASES:
        build_case(rows, name)
    failures = [r["case"] for r in rows if r["status"] == "FAIL"]
    payload = {
        "generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "host": platform.platform(), "bin": BIN, "results": rows,
        "failures": failures,
    }
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("feature_cake (issue #179) regression suite\n%s\n%s\n\n" %
                (payload["host"], BIN))
        for r in rows:
            f.write("%-24s %s  %s\n" %
                    (r["case"], r["status"], r["detail"]))
            for s in r["steps"]:
                f.write("  cmd: %s\n  rc=%s out=%r err=%r\n" %
                        (s["command"], s["rc"], s["stdout"][:600],
                         s["stderr"][:600]))
        f.write("\ncases=%d passed=%d failed=%d\n" %
                (len(rows), len(rows) - len(failures), len(failures)))
    print("feature_cake: %d cases, %d failures" % (len(rows), len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
