#!/usr/bin/env python3
"""Parent-owned regression harness for adversarial-campaign bypasses.

Each case embeds a minimal C source reported by a red agent, then records the
ACTUAL result of `c+ check --strict --sanitize=none` (exit code + stdout/stderr)
against whatever binary CPLUS_BIN points at.  This is deliberately a measurement
tool, not a claim: run it before fixes to capture "accepted", rebuild, and run
again to show which are now "rejected".  No `|| true`, no suppressed failures;
the harness itself always exits 0 and records data (expected findings are not
harness failures).
"""
import json, os, subprocess, sys, datetime

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
OUT = os.path.dirname(os.path.abspath(__file__))
TIMEOUT = 60

CASES = {
    # ---- interprocedural (empirically verified by red agent) ----
    "S2_relay_free": r"""
#include <stdlib.h>
static void inner(int *p){ free(p); }
static void relay(int *p){ inner(p); }
int main(void){ int *p = malloc(sizeof(int)); relay(p); free(p); return 0; }
""",
    "S2_fnptr_free": r"""
#include <stdlib.h>
static void consume(int *p){ free(p); }
int main(void){ int *p = malloc(sizeof(int)); void(*f)(int*)=consume; f(p); free(p); return 0; }
""",
    "S3_return_chain": r"""
#include <stdlib.h>
int *id1(int *p){ return p; }
int *id2(int *p){ return id1(p); }
int main(void){ int *p=malloc(sizeof(int)); int *q=id2(p); free(p); return *q; }
""",
    "S4_null_wrapper": r"""
#include <stddef.h>
static void deref(int *p){ *p = 1; }
static void wrap(int *p){ deref(p); }
int main(void){ wrap(NULL); return 0; }
""",
    "S6_global_alias_uaf": r"""
#include <stdlib.h>
static int *g;
static void setg(int *p){ g = p; }
static void freeg(void){ free(g); }
int main(void){ int *p=malloc(sizeof(int)); setg(p); freeg(); return *p; }
""",
    "S8_self_recursion_oob": r"""
int a[4];
int f(int i){ if(i<=0) return a[0]; return a[i] + f(i-1); }
int main(void){ return f(10); }
""",
    "S1_flag_divergence": r"""
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
""",
    # ---- alias / provenance laundering ----
    "P1_int_launder_uaf": r"""
#include <stdlib.h>
#include <stdint.h>
int main(void){ int *p=malloc(sizeof(int)); uintptr_t u=(uintptr_t)p; int *q=(int*)u; free(p); return *q; }
""",
    "P1_memcpy_alias_uaf": r"""
#include <stdlib.h>
#include <string.h>
int main(void){ int *p=malloc(sizeof(int)); int *q; memcpy(&q,&p,sizeof p); free(p); return *q; }
""",
    "P1_union_pun_uaf": r"""
#include <stdlib.h>
union U { void *p; int *i; };
int main(void){ union U u; u.p=malloc(sizeof(int)); free(u.p); return *u.i; }
""",
    "P1_cast_alias_uaf": r"""
#include <stdlib.h>
int main(void){ int *p=malloc(sizeof(int)); int *q=(int*)(void*)p; free(p); return *q; }
""",
    # ---- temporal / indirect calls ----
    "T1_fnptr_double_free": r"""
#include <stdlib.h>
int main(void){ int *p=malloc(4); if(!p) return 1; void(*f)(void*)=free; f(p); free(p); return 0; }
""",
    "T1_indirect_realloc": r"""
#include <stdlib.h>
#include <stdio.h>
int main(void){ int *p=malloc(4); if(!p)return 1; *p=71;
 void*(*r)(void*,size_t)=realloc; int *q=(int*)r(p,16); if(!q)return 1;
 printf("%d\n",*p); free(q); return 0; }
""",
    # ---- arithmetic / conversion ----
    "A1_stale_nonzero": r"""
static int zero(void){ return 0; }
int f(int d){ if(d){ d=zero(); return 100/d; } return 0; }
int main(void){ return f(1); }
""",
    "A1_compound_assign": r"""
#include <limits.h>
int main(void){ volatile int x=INT_MAX; x+=1; volatile int y=INT_MIN; y-=1; return x+y; }
""",
    "A1_incdec": r"""
#include <limits.h>
int main(void){ volatile int x=INT_MAX; x++; volatile int m=INT_MIN; volatile int n=-m; return x+n; }
""",
    "A1_implicit_narrow": r"""
int f(long x){ char c = x; return c; }
int main(void){ return f(300); }
""",
    "A1_float_to_int": r"""
int f(double d){ return (int)d; }
int main(void){ return f(1e30); }
""",
    # ---- controls that MUST be rejected (harness liveness) ----
    "CTRL_direct_double_free": r"""
#include <stdlib.h>
int main(void){ int *p=malloc(sizeof(int)); free(p); free(p); return 0; }
""",
    "CTRL_direct_null": r"""
#include <stddef.h>
static void deref(int *p){ *p=1; }
int main(void){ deref(NULL); return 0; }
""",
    "CTRL_direct_overflow": r"""
#include <limits.h>
int main(void){ volatile int x=INT_MAX; return x+1; }
""",
}
# The flag-divergence case needs -DDANGER at check time.
EXTRA_ARGS = {"S1_flag_divergence": ["-DDANGER"]}


def main():
    rows = []
    for name, src in CASES.items():
        path = os.path.join(OUT, "case_" + name + ".c")
        with open(path, "w") as f:
            f.write(src)
        cmd = [BIN, "check", "--strict", "--sanitize=none"]
        cmd += EXTRA_ARGS.get(name, [])
        cmd += [path]
        try:
            p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               timeout=TIMEOUT)
            rc, out, err = p.returncode, p.stdout.decode(errors="replace"), p.stderr.decode(errors="replace")
        except subprocess.TimeoutExpired:
            rc, out, err = "TIMEOUT", "", "harness timeout"
        accepted = (rc == 0 and "ok" in out)
        rows.append({"case": name, "rc": rc, "accepted_by_strict": accepted,
                     "stdout": out.strip(), "stderr": err.strip()[:600],
                     "command": " ".join([os.path.basename(BIN)] + cmd[1:])})
        print("%-28s rc=%-8s accepted=%-5s" % (name, rc, accepted))
    payload = {"generated": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "binary": BIN, "mode": "c+ check --strict --sanitize=none",
               "note": "accepted_by_strict=True means the bypass is PRESENT in this binary",
               "results": rows}
    with open(os.path.join(OUT, "bypass_results.json"), "w") as f:
        json.dump(payload, f, indent=2)
    with open(os.path.join(OUT, "bypass_results.txt"), "w") as f:
        f.write("Adversarial bypass verification — %s\n%s\n\n" % (payload["generated"], BIN))
        for r in rows:
            f.write("%-28s rc=%-8s accepted_by_strict=%s\n" %
                    (r["case"], r["rc"], r["accepted_by_strict"]))
            if r["stderr"]:
                f.write("    stderr: %s\n" % r["stderr"].replace("\n", " | ")[:300])
    n_acc = sum(1 for r in rows if r["accepted_by_strict"])
    print("\n%d/%d cases accepted by STRICT (bypresses present)" % (n_acc, len(rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
