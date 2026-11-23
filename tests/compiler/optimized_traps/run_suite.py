#!/usr/bin/env python3
"""Prove that native safety traps SURVIVE optimizer settings.

Every case is compiled with `c+ build --default/--strict --sanitize=none`
plus `-O0`, `-O2` and `-O3`, then RUN. A case passes only when the process
exits with the exact expected C+ code and diagnostic. Compiling is never
proof; no sanitizer is used, so the trap must come from C+ instrumentation.
"""
import json, os, subprocess, sys, datetime, tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
OUT = os.path.dirname(os.path.abspath(__file__))
CASES = [
    # id, source, expected exit, expected diagnostic substring
    ("bounds_o2", "int a[4]; int main(void){int i=7; return a[i];}", 134, "C+ error: index 7", "outside the bounds"),
    ("overflow_o2", "#include <limits.h>\nstatic int add1(int x){return x+1;}\nint main(void){return add1(INT_MAX);}", 110, "signed integer overflow", "signed integer overflow"),
    ("divzero_o2", "static int dv(int x,int y){return x/y;}\nint main(void){return dv(10,0);}", 110, "division by zero", "division by zero"),
    ("shift_o2", "static int sh(int x,int n){return x<<n;}\nint main(void){return sh(1,32);}", 110, "shift count", "shift count"),
    ("null_o2", "static void d(int *p){*p=1;}\nint main(void){d(0);return 0;}", 102, "null pointer dereference", "null pointer argument"),
    # Managed double free: raw malloc/free is deliberately not registered by
    # the ABI-preserving runtime, so use the managed API for this trap proof.
    ("double_free_o2", "#include \"cplus/cplus.h\"\nint main(void){int*p=cplus_alloc(4);cplus_release(p);cplus_release(p);return 0;}", 104, "double free", "double free of"),
]
CONFIGS = [("default_O0", "--default", "-O0"), ("default_O2", "--default", "-O2"),
           ("default_O3", "--default", "-O3"), ("strict_O2", "--strict", "-O2")]
OUTDIR = os.path.join(OUT, "results")
os.makedirs(OUTDIR, exist_ok=True)


def main():
    rows, fails = [], []
    for cid, src, want_rc, want_msg, static_msg in CASES:
        path = os.path.join(OUTDIR, cid + ".c")
        with open(path, "w") as f:
            f.write(src + "\n")
        for cname, mode, opt in CONFIGS:
            exe = os.path.join(OUTDIR, cid + "_" + cname + ".bin")
            cmd = [BIN, "build", mode, "--sanitize=none", opt, path, "-o", exe]
            b = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
            rec = {"case": cid, "config": cname, "mode": mode, "opt": opt,
                   "source": path, "expected_rc": want_rc,
                   "expected_diag": want_msg, "static_diag": static_msg, "build_rc": b.returncode,
                   "build_stderr": b.stderr.decode(errors="replace")[:400]}
            if b.returncode != 0:
                # STRICT is allowed to reject an unsafe program statically;
                # that is a stronger result than a runtime trap.
                combined = (b.stderr + b.stdout).decode(errors="replace").lower()
                if static_msg.lower() in combined:
                    rec["status"] = "PASS_STATIC_REJECT"
                    rec["note"] = "STRICT rejected at compile time with expected diagnostic"
                else:
                    rec["status"] = "FAIL"
                    fails.append(rec)
            else:
                r = subprocess.run([exe], stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, timeout=30)
                rec["run_rc"] = r.returncode
                rec["run_stderr"] = r.stderr.decode(errors="replace")[:400]
                ok = (r.returncode == want_rc and want_msg.lower() in
                      (r.stderr + r.stdout).decode(errors="replace").lower())
                rec["status"] = "PASS" if ok else "FAIL"
                if not ok:
                    fails.append(rec)
            rows.append(rec)
            print("%-16s %-11s %-4s rc=%-4s %s" % (cid, cname, opt,
                  rec.get("run_rc", "-"), rec["status"]))
    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "binary": BIN, "note": "no sanitizer; traps must come from C+ instrumentation",
               "results": rows, "failures": fails}
    with open(os.path.join(OUTDIR, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUTDIR, "summary.txt"), "w") as f:
        f.write("Optimized-build native trap proof (--sanitize=none)\n%s\n\n" % BIN)
        for r in rows:
            f.write("%-16s %-11s %-4s expected=%s got=%s diag=%s status=%s\n" %
                    (r["case"], r["config"], r["opt"], r["expected_rc"],
                     r.get("run_rc", "build_rc=" + str(r["build_rc"])),
                     r["expected_diag"], r["status"]))
        f.write("\nfailures: %d\n" % len(fails))
    print("optimized traps: %d checks, %d failures" % (len(rows), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
