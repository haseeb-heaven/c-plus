#!/usr/bin/env python3
"""Permanent regression suite for the 8 published safety breakers.

For each breaker, `c+ check` must reject in BOTH --default and --strict with a
named diagnostic. A clean rc=0 is a failure; a raw crash is also a failure.
Nothing is merged or suppressed, hard 60s timeouts.
"""
import json, os, subprocess, sys, datetime

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
OUT = os.path.dirname(os.path.abspath(__file__))
CASES = [
    ("01_aliased_uaf_via_struct", "[CPLUS-LIFETIME]"),
    ("02_uaf_address_reuse_f2", "[CPLUS-LIFETIME]"),
    ("03_union_type_confusion", "[CPLUS-FFI]"),
    ("04_heap_oob_via_pointer_param", "[CPLUS-BOUNDS]"),
    ("05_data_race_shared_mut", "[CPLUS-BORROW]"),
    ("06_dangling_stack_out_param", "[CPLUS-LIFETIME]"),
    ("07_func_ptr_format_bypass", "[CPLUS-FFI]"),
    ("08_struct_return_dangling_stack", "[CPLUS-LIFETIME]"),
]
MODES = ["--default", "--strict"]


def main():
    rows, fails = [], []
    for name, tag in CASES:
        src = os.path.join(OUT, "cases", name + ".cp")
        for mode in MODES:
            p = subprocess.run([BIN, "check", mode, "--sanitize=none", src],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               timeout=60)
            out = (p.stdout + p.stderr).decode(errors="replace")
            ok = p.returncode != 0 and tag in out
            rows.append({"case": name, "mode": mode, "rc": p.returncode,
                         "expected_tag": tag, "output": out[:500],
                         "status": "PASS" if ok else "FAIL"})
            if not ok:
                fails.append("%s/%s rc=%s" % (name, mode, p.returncode))
            print("%-32s %-10s %s" % (name, mode, rows[-1]["status"]))
    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "binary": BIN, "results": rows, "failures": fails}
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("Safety breakers: must be blocked in DEFAULT and STRICT\n%s\n\n" % BIN)
        for r in rows:
            f.write("%-32s %-10s %s\n" % (r["case"], r["mode"], r["status"]))
        f.write("\nfailures: %d\n" % len(fails))
    print("safety breakers: %d checks, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
