#!/usr/bin/env python3
"""Prove scan() rejects out-of-range input instead of corrupting an array.

A fixed array of `short` is read with `scan(arr[3])`. Valid input must print
the stored value and exit 0. Out-of-range input must produce a deterministic
C+ diagnostic and CPLUS_EXIT_CONVERSION (111) with --sanitize=none.
"""
import json, os, subprocess, sys, datetime

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
OUT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(OUT, "scan_short.cp")
open(SRC, "w").write('''#include "cplus/cplus.h"
int main(void) {
  short arr[] = {10, 30, 50, 50};
  for (int idx = 0; idx < arr.len; idx++) {
    if (idx == 2) { print("Enter value:"); scan(arr[3]); }
    print("Arr is ", arr[idx]);
  }
  return 0;
}
''')


def run(data):
    return subprocess.run([BIN, "build", "--default", "--sanitize=none", SRC,
                           "-o", os.path.join(OUT, "scan_short.bin")],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=60), data


def main():
    b, _ = run(None)
    if b.returncode:
        print("build failed:", b.stderr.decode()[:400])
        return 1
    exe = os.path.join(OUT, "scan_short.bin")
    good = subprocess.run([exe], input=b"123\n", stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=30)
    bad = subprocess.run([exe], input=b"0xFFFFFF\n", stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, timeout=30)
    gtext = (good.stdout + good.stderr).decode(errors="replace")
    btext = (bad.stdout + bad.stderr).decode(errors="replace")
    ok_good = good.returncode == 0 and "Arr is  123" in gtext
    ok_bad = bad.returncode == 111 and "input overflow" in btext and "arr[3]" in btext
    payload = {"generated": datetime.timezone.utc and datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "binary": BIN, "valid_input_rc": good.returncode,
               "valid_output": gtext[:400], "overflow_rc": bad.returncode,
               "overflow_output": btext[:400], "pass": ok_good and ok_bad}
    json.dump(payload, open(os.path.join(OUT, "summary.json"), "w"), indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("scan() input-range safety\n")
        f.write("valid: rc=%s output=%r\n" % (good.returncode, gtext[:200]))
        f.write("overflow: rc=%s output=%r\n" % (bad.returncode, btext[:200]))
        f.write("pass=%s\n" % payload["pass"])
    print("valid rc=%s overflow rc=%s pass=%s" %
          (good.returncode, bad.returncode, payload["pass"]))
    return 0 if payload["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
