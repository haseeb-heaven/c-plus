#!/usr/bin/env python3
"""Default strict/warnings-as-errors policy regression suite (issue #34).

Each case records the exact command, exit code, stdout and stderr. No `|| true`,
no skips and no suppressed failures.

Run: CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/default_policy/run_suite.py
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
CASE_DIR = os.path.join(HERE, "cases")
OUT = os.path.join(HERE, "results")
os.makedirs(CASE_DIR, exist_ok=True)
os.makedirs(OUT, exist_ok=True)

WARN = "#include <stddef.h>\nstatic void deref(int *p){ *p=1; }\nint main(void){ deref(NULL); return 0; }\n"
CLEAN_C = "#include <stdio.h>\nint main(void){ printf(\"clean\\n\"); return 0; }\n"
CLEAN_CP = "#include <stdio.h>\nint main(void){ printf(\"clean-cp\\n\"); return 0; }\n"
HOST_WARN = "int main(void){ int unused=1; (void)0; return 0; }\n"

CASES = [
    dict(name="clean_c_no_flags", src=CLEAN_C, mode=[], expect=0, out="clean\n"),
    dict(name="clean_cp_no_flags", src=CLEAN_CP, mode=[], expect=0, out="clean-cp\n"),
    dict(name="clean_c_werror", src=CLEAN_C, mode=["-Werror"], expect=0, out="clean\n"),
    dict(name="cplus_warning_check_default", src=WARN, cmd="check", mode=[],
         expect=1, diag="[CPLUS-LIFETIME]"),
    dict(name="cplus_warning_build_default", src=WARN, mode=[],
         expect=1, diag="C+ warning(s) treated as errors"),
    dict(name="cplus_warning_check_werror", src=WARN, cmd="check", mode=["-Werror"],
         expect=1, diag="[CPLUS-LIFETIME]"),
    dict(name="cplus_warning_check_w", src=WARN, cmd="check", mode=["-w"],
         expect=1, diag="[CPLUS-LIFETIME]"),
    dict(name="cplus_warning_check_release", src=WARN, cmd="check", mode=["--release"],
         expect=1, diag="[CPLUS-LIFETIME]"),
    dict(name="cplus_warning_check_no_werror", src=WARN, cmd="check",
         mode=["--no-werror"], expect=0),
    dict(name="cplus_warning_legacy_accepted", src=WARN, cmd="check",
         mode=["--legacy-c"], expect=0),
    dict(name="legacy_werror_explicit", src=WARN, cmd="check",
         mode=["--legacy-c", "-Werror"], expect=0),
    dict(name="host_warning_default", src=HOST_WARN, mode=["-Wall"],
         expect=1, diag="-Werror,-Wunused-variable"),
    dict(name="host_warning_with_w", src=HOST_WARN, mode=["-Wall", "-w"],
         expect=1, diag="-Werror,-Wunused-variable"),
    dict(name="host_warning_legacy_w", src=HOST_WARN,
         mode=["--legacy-c", "-Wall", "-w"], expect=0),
]


def run(argv, timeout=90):
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


def main():
    rows, fails = [], []
    for c in CASES:
        src = os.path.join(CASE_DIR, c["name"] + ".c")
        with open(src, "w") as f:
            f.write(c["src"])
        exe = os.path.join(OUT, c["name"] + ".bin")
        if os.path.exists(exe):
            os.unlink(exe)
        cmd = c["mode"] + (["--sanitize=none"] if c.get("cmd") == "check" else [])
        if c.get("cmd") == "check":
            argv = [BIN, "check", *cmd, src]
        else:
            argv = [BIN, "build", *cmd, src, "-o", exe]
        step = run(argv)
        ok = (step["rc"] == c["expect"] and not step["timed_out"] and
              c.get("diag", "") in (step["stdout"] + step["stderr"]))
        steps = [step]
        if c["expect"] == 0 and c.get("out"):
            r = run([exe])
            steps.append(r)
            ok = ok and r["rc"] == 0 and r["stdout"] == c["out"]
        if c["expect"] != 0:
            ok = ok and (c.get("cmd") == "check" or not os.path.exists(exe))
        row = {"case": c["name"], "status": "PASS" if ok else "FAIL",
               "expected_rc": c["expect"], "steps": steps}
        rows.append(row)
        if not ok:
            fails.append(c["name"])
        print("%-34s %s" % (c["name"], row["status"]))

    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "host": platform.platform(), "compiler": BIN,
               "results": rows, "failures": fails}
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("Default policy suite (issue #34)\n%s\n%s\n\n" % (payload["host"], BIN))
        for r in rows:
            f.write("%-34s %s\n" % (r["case"], r["status"]))
            for s in r["steps"]:
                f.write("  cmd: %s\n  rc=%s out=%r err=%r\n" %
                        (" ".join(s["command"]), s["rc"], s["stdout"][:200],
                         s["stderr"][:400]))
        f.write("\ncases=%d passed=%d failed=%d\n" %
                (len(rows), len(rows) - len(fails), len(fails)))
    print("default_policy: %d cases, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
