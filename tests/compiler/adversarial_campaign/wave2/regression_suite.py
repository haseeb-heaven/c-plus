#!/usr/bin/env python3
"""Wave 2 BLUE DEFENDER -- permanent regression suite for wave 1.

This is NOT a measurement tool like ../verify_bypasses.py. It ASSERTs the
intended outcome for every wave-1 case:

  * 10 `closed` cases: must now be REJECTED by
    `c+ check --strict --sanitize=none` AND emit the expected diagnostic
    category tag ([CPLUS-OWNER], [CPLUS-LIFETIME], [CPLUS-BOUNDS],
    [CPLUS-ARITH]). We assert the CATEGORY, not just the exit code.
  * 6 `known_open` cases: recorded as KNOWN_OPEN if still accepted -- this is
    NOT a suite failure. If a later wave rejects one, it is reported as
    NEWLY_CLOSED (progress), also not a failure. Either way the status is
    visible in summary.txt / summary.json so regressions are hard to miss.
  * 3 `control` cases: must be rejected with their expected category; these
    prove the checker is live. A failure here fails the whole suite.

No `|| true`, no suppressed failures: the process exits 0 only if every
closed case and every control is rejected with the expected category, and
exits 1 otherwise. Every subprocess uses a hard timeout (TIMEOUT seconds).

Run:
  CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/adversarial_campaign/wave2/regression_suite.py
"""
import datetime
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# wave2 -> adversarial_campaign -> compiler -> tests -> repo root
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
CASES_DIR = os.path.join(HERE, "cases")
TIMEOUT = 60  # hard timeout, never disabled

OWNER = "[CPLUS-OWNER]"
LIFETIME = "[CPLUS-LIFETIME]"
BOUNDS = "[CPLUS-BOUNDS]"
ARITH = "[CPLUS-ARITH]"
FFI = "[CPLUS-FFI]"

# kind: closed | known_open | control
# categories: required bracketed diagnostic tags (asserted for closed/control)
CASES = [
    # ---------------- 10 CLOSED (must be rejected, category asserted) --------
    dict(name="S2_relay_free", kind="closed", categories=[OWNER], src=r"""
#include <stdlib.h>
static void inner(int *p){ free(p); }
static void relay(int *p){ inner(p); }
int main(void){ int *p = malloc(sizeof(int)); relay(p); free(p); return 0; }
"""),
    dict(name="S3_return_chain", kind="closed", categories=[LIFETIME], src=r"""
#include <stdlib.h>
int *id1(int *p){ return p; }
int *id2(int *p){ return id1(p); }
int main(void){ int *p=malloc(sizeof(int)); int *q=id2(p); free(p); return *q; }
"""),
    dict(name="S4_null_wrapper", kind="closed", categories=[LIFETIME], src=r"""
#include <stddef.h>
static void deref(int *p){ *p = 1; }
static void wrap(int *p){ deref(p); }
int main(void){ wrap(NULL); return 0; }
"""),
    dict(name="S8_self_recursion_oob", kind="closed", categories=[BOUNDS], src=r"""
int a[4];
int f(int i){ if(i<=0) return a[0]; return a[i] + f(i-1); }
int main(void){ return f(10); }
"""),
    dict(name="S1_flag_divergence", kind="closed", categories=[LIFETIME, OWNER],
         extra_args=["-DDANGER"], src=r"""
#include <stdlib.h>
static void consume(int *p){ free(p); }
int main(void){
  int *p = malloc(sizeof(int));
#ifdef DANGER
  consume(p); consume(p);
#else
  free(p);
#endif
  return 0;
}
"""),
    dict(name="P1_int_launder_uaf", kind="closed", categories=[LIFETIME], src=r"""
#include <stdlib.h>
#include <stdint.h>
int main(void){ int *p=malloc(sizeof(int)); uintptr_t u=(uintptr_t)p; int *q=(int*)u; free(p); return *q; }
"""),
    dict(name="P1_union_pun_uaf", kind="closed", categories=[LIFETIME], src=r"""
#include <stdlib.h>
union U { void *p; int *i; };
int main(void){ union U u; u.p=malloc(sizeof(int)); free(u.p); return *u.i; }
"""),
    dict(name="T1_indirect_realloc", kind="closed", categories=[LIFETIME], src=r"""
#include <stdlib.h>
#include <stdio.h>
int main(void){ int *p=malloc(4); if(!p)return 1; *p=71;
 void*(*r)(void*,size_t)=realloc; int *q=(int*)r(p,16); if(!q)return 1;
 printf("%d\n",*p); free(q); return 0; }
"""),
    dict(name="A1_stale_nonzero", kind="closed", categories=[ARITH], src=r"""
static int zero(void){ return 0; }
int f(int d){ if(d){ d=zero(); return 100/d; } return 0; }
int main(void){ return f(1); }
"""),
    dict(name="A1_incdec", kind="closed", categories=[ARITH], src=r"""
#include <limits.h>
int main(void){ volatile int x=INT_MAX; x++; volatile int m=INT_MIN; volatile int n=-m; return x+n; }
"""),

    # ---------------- 6 STILL OPEN (KNOWN_OPEN / NEWLY_CLOSED) ---------------
    dict(name="S2_fnptr_free", kind="known_open", categories=[], src=r"""
#include <stdlib.h>
static void consume(int *p){ free(p); }
int main(void){ int *p = malloc(sizeof(int)); void(*f)(int*)=consume; f(p); free(p); return 0; }
"""),
    dict(name="S6_global_alias_uaf", kind="known_open", categories=[], src=r"""
#include <stdlib.h>
static int *g;
static void setg(int *p){ g = p; }
static void freeg(void){ free(g); }
int main(void){ int *p=malloc(sizeof(int)); setg(p); freeg(); return *p; }
"""),
    dict(name="T1_fnptr_double_free", kind="known_open", categories=[], src=r"""
#include <stdlib.h>
int main(void){ int *p=malloc(4); if(!p) return 1; void(*f)(void*)=free; f(p); free(p); return 0; }
"""),
    dict(name="A1_compound_assign", kind="known_open", categories=[], src=r"""
#include <limits.h>
int main(void){ volatile int x=INT_MAX; x+=1; volatile int y=INT_MIN; y-=1; return x+y; }
"""),
    dict(name="A1_implicit_narrow", kind="known_open", categories=[], src=r"""
int f(long x){ char c = x; return c; }
int main(void){ return f(300); }
"""),
    dict(name="A1_float_to_int", kind="known_open", categories=[], src=r"""
int f(double d){ return (int)d; }
int main(void){ return f(1e30); }
"""),

    # ---------------- 3 CONTROLS (must be rejected) --------------------------
    dict(name="CTRL_direct_double_free", kind="control", categories=[OWNER], src=r"""
#include <stdlib.h>
int main(void){ int *p=malloc(sizeof(int)); free(p); free(p); return 0; }
"""),
    dict(name="CTRL_direct_null", kind="control", categories=[LIFETIME], src=r"""
#include <stddef.h>
static void deref(int *p){ *p=1; }
int main(void){ deref(NULL); return 0; }
"""),
    dict(name="CTRL_direct_overflow", kind="control", categories=[ARITH], src=r"""
#include <limits.h>
int main(void){ volatile int x=INT_MAX; return x+1; }
"""),
]

TAG_RE = re.compile(r"\[CPLUS-[A-Z-]+\]")


def categories_in(text):
    return sorted(set(TAG_RE.findall(text)))


def main():
    os.makedirs(CASES_DIR, exist_ok=True)
    results = []
    for case in CASES:
        name, kind = case["name"], case["kind"]
        path = os.path.join(CASES_DIR, "case_" + name + ".c")
        with open(path, "w") as f:
            f.write(case["src"])

        cmd = [BIN, "check", "--strict", "--sanitize=none"]
        cmd += case.get("extra_args", [])
        cmd += [path]
        timed_out = False
        try:
            p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               timeout=TIMEOUT)
            rc, out, err = (p.returncode, p.stdout.decode(errors="replace"),
                            p.stderr.decode(errors="replace"))
        except subprocess.TimeoutExpired:
            timed_out = True
            rc, out, err = "TIMEOUT", "", "hard timeout (%ds) exceeded" % TIMEOUT

        accepted = (rc == 0 and "ok" in out)
        diag = err if err.strip() else out
        found = categories_in(diag)
        expected = case.get("categories", [])
        missing = [c for c in expected if c not in found]

        if kind in ("closed", "control"):
            if timed_out:
                status = "FAIL"           # cannot prove rejection
            elif accepted or rc == 0:
                status = "FAIL"           # bypass still present
            elif missing:
                status = "FAIL"           # rejected, but wrong/unknown category
            else:
                status = "PASS"
        else:  # known_open
            if timed_out:
                status = "ANOMALY"        # unexpected: cannot classify
            elif accepted:
                status = "KNOWN_OPEN"     # expected, not a failure
            else:
                status = "NEWLY_CLOSED"   # progress, not a failure

        results.append({
            "case": name,
            "kind": kind,
            "status": status,
            "command": " ".join([os.path.basename(BIN)] + cmd[1:]),
            "rc": rc,
            "accepted_by_strict": accepted,
            "expected_categories": expected,
            "categories_found": found,
            "missing_categories": missing,
            "timed_out": timed_out,
            "stdout": out.strip(),
            "stderr": err.strip(),
        })
        print("%-28s %-11s %-13s rc=%-8s accepted=%-5s" %
              (name, kind, status, rc, accepted))

    closed = [r for r in results if r["kind"] == "closed"]
    controls = [r for r in results if r["kind"] == "control"]
    opens = [r for r in results if r["kind"] == "known_open"]
    known_open = [r["case"] for r in opens if r["status"] == "KNOWN_OPEN"]
    newly_closed = [r["case"] for r in opens if r["status"] == "NEWLY_CLOSED"]
    anomalies = [r["case"] for r in results if r["status"] == "ANOMALY"]

    counts = {
        "total": len(results),
        "closed_asserted_rejected": sum(1 for r in closed if r["status"] == "PASS"),
        "closed_failed": sum(1 for r in closed if r["status"] == "FAIL"),
        "controls_asserted_rejected": sum(1 for r in controls if r["status"] == "PASS"),
        "controls_failed": sum(1 for r in controls if r["status"] == "FAIL"),
        "known_open": len(known_open),
        "newly_closed": len(newly_closed),
        "anomalies": len(anomalies),
    }
    suite_passed = (counts["closed_failed"] == 0 and
                    counts["controls_failed"] == 0 and
                    counts["anomalies"] == 0)

    payload = {
        "generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "binary": BIN,
        "mode": "c+ check --strict --sanitize=none",
        "timeout_seconds": TIMEOUT,
        "policy": ("closed/control: reject + assert category tag; "
                   "known_open: KNOWN_OPEN if accepted, NEWLY_CLOSED if rejected; "
                   "no || true, hard timeouts"),
        "suite_passed": suite_passed,
        "counts": counts,
        "known_open_list": known_open,
        "newly_closed_list": newly_closed,
        "results": results,
    }
    with open(os.path.join(HERE, "summary.json"), "w") as f:
        json.dump(payload, f, indent=2)
        f.write("\n")

    with open(os.path.join(HERE, "summary.txt"), "w") as f:
        f.write("Wave 2 BLUE DEFENDER regression suite (wave-1 bypasses)\n")
        f.write("Generated: %s\n" % payload["generated"])
        f.write("Binary:    %s\n" % BIN)
        f.write("Mode:      %s\n" % payload["mode"])
        f.write("Timeout:   %ds (hard)\n\n" % TIMEOUT)
        f.write("PER-CASE RESULTS\n")
        for r in results:
            f.write("- %-28s %-11s %-13s rc=%-8s accepted=%s\n" %
                    (r["case"], r["kind"], r["status"], r["rc"],
                     r["accepted_by_strict"]))
            f.write("    command: %s\n" % r["command"])
            if r["expected_categories"]:
                f.write("    expected categories: %s | found: %s | missing: %s\n" %
                        (", ".join(r["expected_categories"]),
                         ", ".join(r["categories_found"]) or "(none)",
                         ", ".join(r["missing_categories"]) or "(none)"))
            diag = r["stderr"] or r["stdout"]
            if diag:
                for line in diag.splitlines():
                    f.write("    diag: %s\n" % line.strip())
            f.write("\n")
        f.write("KNOWN_OPEN (%d): %s\n" %
                (len(known_open), ", ".join(known_open) or "(none)"))
        f.write("NEWLY_CLOSED (%d): %s\n" %
                (len(newly_closed), ", ".join(newly_closed) or "(none)"))
        if anomalies:
            f.write("ANOMALIES (%d): %s\n" %
                    (len(anomalies), ", ".join(anomalies)))
        f.write("\nCOUNTS\n")
        for k, v in counts.items():
            f.write("  %-28s %d\n" % (k, v))
        f.write("\nSUITE PASSED: %s\n" % suite_passed)

    print("\nKNOWN_OPEN (%d): %s" % (len(known_open), ", ".join(known_open)))
    print("NEWLY_CLOSED (%d): %s" % (len(newly_closed), ", ".join(newly_closed)))
    print("COUNTS: %s" % json.dumps(counts, sort_keys=True))
    print("SUITE PASSED: %s" % suite_passed)
    return 0 if suite_passed else 1


if __name__ == "__main__":
    sys.exit(main())
