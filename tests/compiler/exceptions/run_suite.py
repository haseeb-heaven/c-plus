#!/usr/bin/env python3
"""C+ exception syntax: safe-mode lowering + LEGACY source preservation.

Cases:
- demo.cp: the user's try/catch program with arr.len and scan() compiles and
  runs; invalid runtime input still produces the named rc=111 conversion trap.
- throw/catch: `throw "boom"` is caught, handler sees code and message, and
  execution continues after the try.
- nested try: inner throw is handled by the inner catch, outer continues.
- LEGACY: emitted C is byte-identical to source after one leading #line.
"""
import json, os, re, subprocess, sys, datetime

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
OUT = os.path.dirname(os.path.abspath(__file__))
CASES = {
    "throw_catch": '''#include "cplus/cplus.h"
int main(void){ try { print("before"); throw "boom"; print("after"); }
 catch (Exception ex) { print("caught", ex.code, ex.message); }
 print("done"); return 0; }
''',
    "nested": '''#include "cplus/cplus.h"
int main(void){ try { try { throw 42; } catch (Exception e) { print("inner", e.code); }
 print("middle"); } catch (Exception e) { print("outer"); } print("end"); return 0; }
''',
    "demo": '''#include "cplus/cplus.h"
int main(void) {
  try { short arr[] = {10,30,50,50};
    for (int idx=0; idx<arr.len; idx++) {
      if (idx==2) { print("Enter value:"); scan(arr[3]); }
      print("Arr is ", arr[idx]);
    }
  } catch (Exception ex) { print("heaven here ;)"); }
  return 0;
}
''',
}


def run(args, data=None):
    return subprocess.run(args, input=(data.encode() if data else None),
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=60)


def main():
    rows, fails = [], []
    for name, src in CASES.items():
        path = os.path.join(OUT, name + ".cp")
        open(path, "w").write(src)
        exe = os.path.join(OUT, name + ".bin")
        b = run([BIN, "build", "--default", "--sanitize=none", path, "-o", exe])
        rec = {"case": name, "build_rc": b.returncode,
               "build_stderr": b.stderr.decode(errors="replace")[:400]}
        if name == "throw_catch":
            r = run([exe])
            rec["run_rc"] = r.returncode
            rec["stdout"] = r.stdout.decode(errors="replace")
            ok = rec["stdout"] == "before\ncaught 1 boom\ndone\n" and r.returncode == 0
        elif name == "nested":
            r = run([exe])
            rec["run_rc"] = r.returncode
            rec["stdout"] = r.stdout.decode(errors="replace")
            ok = rec["stdout"] == "inner 42\nmiddle\nend\n" and r.returncode == 0
        else:
            good = run([exe], "123\n")
            bad = run([exe], "0xFFFFFF\n")
            rec["good_rc"] = good.returncode
            rec["bad_rc"] = bad.returncode
            rec["bad_stderr"] = bad.stderr.decode(errors="replace")[:300]
            ok = (good.returncode == 0 and "Arr is  123" in good.stdout.decode() and
                  bad.returncode == 111 and "input overflow" in rec["bad_stderr"])
        # LEGACY preserves the original source bytes after #line.
        gen = os.path.join(OUT, name + ".legacy.c")
        lb = run([BIN, "build", "--legacy-c", "--sanitize=none", "--emit-c",
                  gen, path, "-o", os.path.join(OUT, name + ".legacy.bin")])
        raw = open(path, "rb").read()
        em = open(gen, "rb").read() if os.path.exists(gen) else b""
        m = re.match(rb'^#line \d+ "[^"]*"\n', em)
        rec["legacy_build_rc"] = lb.returncode
        rec["legacy_source_identical"] = bool(m) and em[m.end():] == raw
        # LEGACY preserves ordinary C bytes; C+ syntax such as `arr.len` is
        # still lowered, and `try` is not plain C (so its build may fail).
        if name != "demo":
            ok = ok and rec["legacy_source_identical"]
        rec["status"] = "PASS" if ok else "FAIL"
        rows.append(rec)
        if not ok:
            fails.append(name)
        print("%-14s %s" % (name, rec["status"]))
    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "binary": BIN, "results": rows, "failures": fails}
    json.dump(payload, open(os.path.join(OUT, "summary.json"), "w"), indent=2)
    with open(os.path.join(OUT, "summary.txt"), "w") as f:
        f.write("C+ exception handling evidence\n")
        for r in rows:
            f.write("%-14s %s\n" % (r["case"], r["status"]))
        f.write("\nfailures: %d\n" % len(fails))
    print("exceptions: %d cases, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
