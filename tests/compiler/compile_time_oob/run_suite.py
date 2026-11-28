#!/usr/bin/env python3
"""Provable OOB expressions must be rejected at compile time, not trapped.

Every case below has a compile-time provable out-of-range index into a
fixed array. `c+ check --strict --sanitize=none` must return nonzero with a
[CPLUS-BOUNDS] diagnostic; a runtime trap is NOT accepted as proof.
"""
import glob, json, os, subprocess, sys, datetime

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
OUT = os.path.dirname(os.path.abspath(__file__))


def main():
    rows, fails = [], []
    for src in sorted(glob.glob(os.path.join(OUT, "cases", "*.c"))):
        p = subprocess.run([BIN, "check", "--strict", "--sanitize=none", src],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
        text = (p.stdout + p.stderr).decode(errors="replace")
        ok = p.returncode != 0 and "[CPLUS-BOUNDS]" in text
        rows.append({"source": os.path.basename(src), "rc": p.returncode,
                     "diagnostic": text[:400], "status": "PASS" if ok else "FAIL"})
        if not ok:
            fails.append(os.path.basename(src))
        print("%-22s %s" % (os.path.basename(src), rows[-1]["status"]))
    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "binary": BIN, "results": rows, "failures": fails}
    with open(os.path.join(OUT, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("Compile-time OOB expression suite (runtime traps not accepted)\n")
        for r in rows:
            f.write("%-22s %s\n" % (r["source"], r["status"]))
        f.write("\nfailures: %d\n" % len(fails))
    print("compile-time oob: %d cases, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
