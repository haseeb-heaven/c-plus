#!/usr/bin/env python3
"""Deterministic SEEDED parallel generator of NULL-STATE INVALIDATION (>=100 cases).

Produces distinct, self-contained, syntactically valid C translation units that
exercise how a *tracked null fact* for a pointer is created, propagated and
invalidated.  Every case declares one expected outcome (ground truth for valid
C semantics, not the current checker's behaviour):

  reject - proven null dereference (or dereference of a pointer that is
           provably null on every path): must fail BOTH --default and --strict.
           Acceptance in either mode is a false negative / safety hole.
  strict - conditional / runtime-dependent null dereference: MAY pass
           --default, but must fail --strict (acceptance there is a hole).
  accept - safe control: the null fact was correctly invalidated (reassignment
           to malloc/&local/a non-null alias, a real null guard, free(NULL),
           reset-after-free) or never dereferenced: must pass BOTH modes
           (rejection is a false positive).

Families covered:

  null_to_malloc      p=NULL; p=malloc/calloc; use; free            (accept)
  null_to_addr        p=NULL; p=&local / &arr[i]; deref              (accept)
  branch_merge        p=NULL in one/both branch arms, then merged use
                      (unguarded -> strict/reject, guarded or
                      reassigned after merge -> accept)
  loop_reassign       p reassigned (or not) inside for/while/do loops
  assign_from_q       p=q where q is null (reject) or non-null (accept)
  ptr_arith_after_null p++, p+=k, p--, ++p after null, then deref     (reject)
  free_after_null     free(p) with p null is a no-op                 (accept)
  null_after_free     free(p); p=NULL; guarded use (accept) or naked
                      deref (reject)
  alias_twohop        null state through 2-3 hop aliases, struct field,
                      array slot; plus invalidation through the chain
  interproc           null state across function calls (param deref,
                      two-hop callee, out-parameter set/reset)
  control_reject      explicit must-reject null dereference controls
  control_accept      explicit must-accept safe controls

Programs come from deterministic seeded sweeps plus a seeded RNG (FUZZ_SEED);
each program carries a unique marker and the final pass re-rolls parameters
until TARGET distinct bodies exist.  Cases run in parallel (FUZZ_WORKERS) with
a per-check timeout.  Mismatches are saved (source + full diagnostics) under
tests/out/core/nl_invalidate/failures/.  This is a differential measurement,
not a proof of safety.

Environment:
  FUZZ_SEED    default 0xB1A5E2
  FUZZ_WORKERS default 8
  CPLUS_BIN    default <root>/build/c+
  NL_TARGET    default 140 (minimum 100)
  TIMEOUT      fixed at 60s per check
"""
import concurrent.futures
import os
import pathlib
import random
import subprocess
import sys
from collections import Counter

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/nl_invalidate"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
CASES_DIR.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0xB1A5E2"), 0)
TARGET = max(100, int(os.environ.get("NL_TARGET", "140")))
TIMEOUT = 60

CASES = []  # (family, name, body, expect)
SEEN = set()  # exact generated program bodies, enforces distinctness
RNG = random.Random(SEED)

STD = "#include <stdlib.h>\n"  # required so that NULL / malloc / free are declared
TYPES = ("int", "long", "short", "char")


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in f-strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def val():
    return RNG.randint(1, 97)


def nlen():
    """Array length (at least 3 so in-bounds index helpers stay valid)."""
    return RNG.randint(3, 12)


def off():
    return RNG.randint(1, 3)


def ty():
    return RNG.choice(TYPES)


def add(family, name, body, expect):
    """Append a case only if its exact marked program body is new."""
    assert expect in ("reject", "strict", "accept"), (name, expect)
    normalized = (f"/* nl_invalidate seed=0x{SEED:X} case={name} family={family} */\n"
                  + body.strip() + "\n")
    if normalized in SEEN:
        return False
    SEEN.add(normalized)
    CASES.append((family, name, normalized, expect))
    return True


def params():
    """One deterministic bundle of seeded template parameters."""
    n = nlen()
    v = val()
    return dict(T=ty(), V=v, V2=v + 1, N=n, NN=n - 1,
                K=RNG.randrange(n), K1=RNG.randrange(1, n),
                K2=RNG.randrange(n - 1), KK=off())


# ---------------------------------------------------------------------------
# Per-family templates.  Each pattern is (printf-like template, expectation).
# ---------------------------------------------------------------------------

# 1. p=NULL then p=malloc/calloc then deref -- safe invalidation.
P_NULL_TO_MALLOC = [
    ("int main(void){@@T@@*p=NULL;p=malloc(sizeof(@@T@@));if(!p)return 2;"
     "*p=@@V@@;@@T@@ v=*p;free(p);return v==@@V@@?0:1;}", "accept"),
    ("int main(void){@@T@@*p=NULL;p=calloc(1,sizeof(@@T@@));if(!p)return 2;"
     "*p=@@V@@;@@T@@ v=*p;free(p);return v==@@V@@?0:1;}", "accept"),
    ("int main(void){@@T@@*p=NULL;p=malloc(@@N@@*sizeof(@@T@@));if(!p)return 2;"
     "p[@@K@@]=@@V@@;@@T@@ v=p[@@K@@];free(p);return v==@@V@@?0:1;}", "accept"),
    ("int main(void){@@T@@*p=(@@T@@*)NULL;p=malloc(sizeof(@@T@@));if(!p)return 2;"
     "*p=@@V@@;@@T@@ v=*p;free(p);return v==@@V@@?0:1;}", "accept"),
]

# 2. p=NULL then p=&local -- safe invalidation.
P_NULL_TO_ADDR = [
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;p=&x;return *p==@@V@@?0:1;}", "accept"),
    ("int main(void){@@T@@ a[@@N@@]={0};@@T@@*p=NULL;p=&a[@@K@@];"
     "*p=@@V@@;return a[@@K@@]==@@V@@?0:1;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=(@@T@@*)NULL;p=&x;"
     "return *p==@@V@@?0:1;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;p=&x;*p=@@V2@@;"
     "return x==@@V2@@?0:1;}", "accept"),
    ("int main(void){@@T@@ a[@@N@@]={0};@@T@@*p=NULL;p=a;"
     "return p==&a[0]?0:1;}", "accept"),
]

# 3. p=NULL in a branch, then merged use (guarded/reassigned forms are safe).
P_BRANCH_MERGE = [
    ("int main(int argc,char**v){(void)v;@@T@@ x=@@V@@;@@T@@*p=&x;"
     "if(argc)p=NULL;return *p;}", "strict"),
    ("int main(int argc,char**v){(void)v;@@T@@ x=@@V@@;@@T@@*p=&x;"
     "if(argc)p=NULL;else p=&x;return *p;}", "strict"),
    ("int main(int argc,char**v){(void)v;@@T@@ x=@@V@@;@@T@@*p=&x;"
     "if(argc)p=NULL;else p=NULL;return *p;}", "reject"),
    ("int main(int argc,char**v){(void)v;@@T@@ x=@@V@@;@@T@@*p=&x;"
     "if(argc)p=NULL;return p?*p:0;}", "accept"),
    ("int main(int argc,char**v){(void)v;@@T@@ x=@@V@@;@@T@@*p=&x;"
     "if(argc)p=NULL;else p=&x;p=&x;return *p==@@V@@?0:1;}", "accept"),
    ("int main(int argc,char**v){(void)v;@@T@@ x=@@V@@;@@T@@*p=NULL;"
     "if(argc)p=&x;return *p;}", "strict"),
    ("int main(int argc,char**v){(void)v;@@T@@ x=@@V@@;@@T@@*p=NULL;"
     "if(argc)p=&x;return p?*p:0;}", "accept"),
    ("int main(int argc,char**v){(void)v;@@T@@ x=@@V@@;@@T@@*p=&x;"
     "if(argc)p=&x;else p=NULL;return *p;}", "strict"),
    ("int main(int argc,char**v){(void)v;@@T@@ x=@@V@@;@@T@@*p=&x;"
     "if(argc){if(argc>1)p=NULL;}return *p;}", "strict"),
    ("int main(int argc,char**v){(void)v;@@T@@ x=@@V@@;@@T@@*p=&x;"
     "if(argc)p=NULL;return p==NULL?0:*p;}", "accept"),
    ("int main(int argc,char**v){(void)v;@@T@@ x=@@V@@;@@T@@*p=&x;"
     "if(argc)p=NULL;else p=&x;*p=@@V2@@;return *p==@@V2@@?0:1;}", "strict"),
    ("int main(int argc,char**v){(void)v;@@T@@ x=@@V@@;@@T@@*p=&x;"
     "if(argc)p=NULL;p=&x;return *p==@@V@@?0:1;}", "accept"),
]

# 4. p reassigned (or not) inside a loop.
P_LOOP_REASSIGN = [
    ("int main(void){@@T@@ a[@@N@@]={0};@@T@@*p=NULL;"
     "for(int i=0;i<@@N@@;i++)p=&a[i];return *p==0?0:1;}", "accept"),
    ("int main(void){@@T@@ a[@@N@@]={0};@@T@@*p=NULL;"
     "for(int i=0;i<@@N@@;i++){p=&a[i];}return p==&a[@@NN@@]?0:1;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=&x;"
     "for(int i=0;i<@@N@@;i++){if(i==@@K@@)p=NULL;}return *p;}", "reject"),
    ("int main(void){@@T@@*p=NULL;"
     "for(int i=0;i<@@N@@;i++){(void)i;}return *p;}", "reject"),
    ("int main(int argc,char**v){(void)v;@@T@@ x=@@V@@;@@T@@*p=&x;"
     "for(int i=0;i<@@N@@;i++){if(argc)p=NULL;}return *p;}", "strict"),
    ("int main(void){@@T@@ a[@@N@@]={0};@@T@@*p=NULL;int i=0;"
     "while(i<@@N@@){p=&a[i];i++;}return *p==0?0:1;}", "accept"),
    ("int main(void){@@T@@ a[@@N@@]={0};@@T@@*p=NULL;"
     "do{p=&a[0];}while(0);return *p==0?0:1;}", "accept"),
    ("int main(void){@@T@@ a[@@N@@]={0};@@T@@*p=NULL;"
     "for(int i=0;i<@@N@@;i++){p=&a[i];*p=@@V@@;}return a[@@NN@@]==@@V@@?0:1;}", "accept"),
]

# 5. p=q where q is null (reject) or non-null (accept).
P_ASSIGN_FROM_Q = [
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=&x;@@T@@*q=NULL;p=q;return *p;}", "reject"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=&x;@@T@@*q=(@@T@@*)NULL;p=q;return *p;}", "reject"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=&x;@@T@@*q=0;p=q;*p=@@V2@@;return 0;}", "reject"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;@@T@@*q=&x;p=q;"
     "return *p==@@V@@?0:1;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@ y=@@V2@@;@@T@@*p=NULL;@@T@@*q=&x;"
     "p=q;p=&y;return *p==@@V2@@?0:1;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=&x;@@T@@*q=NULL;p=q;"
     "return p?*p:0;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*q=&x;q=NULL;@@T@@*p=&x;p=q;return *p;}", "reject"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*q=NULL;q=&x;@@T@@*p=NULL;p=q;"
     "return *p==@@V@@?0:1;}", "accept"),
]

# 6. pointer arithmetic after null, then deref.
P_PTR_ARITH = [
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=&x;p=NULL;p++;return *p;}", "reject"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=&x;p=NULL;p--;return *p;}", "reject"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=&x;p=NULL;p+=@@KK@@;return *p;}", "reject"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=&x;p=NULL;++p;return *p;}", "reject"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=&x;p=NULL;p++;*p=@@V2@@;return 0;}", "reject"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;@@T@@*q=p+@@KK@@;return *q;}", "reject"),
    ("int main(void){@@T@@ a[@@N@@]={0};@@T@@*p=&a[@@K1@@];p--;"
     "return *p==0?0:1;}", "accept"),
    ("int main(void){@@T@@ a[@@N@@]={0};@@T@@*p=&a[0];p+=@@K2@@;"
     "return p==&a[@@K2@@]?0:1;}", "accept"),
    ("int main(void){@@T@@ a[@@N@@]={0};@@T@@*p=NULL;p=&a[0];p+=@@K2@@;"
     "(void)p;return 0;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;p=&x;p=&x;"
     "return *p==@@V@@?0:1;}", "accept"),
]

# 7. free(p) with p null is a defined no-op.
P_FREE_AFTER_NULL = [
    ("int main(void){@@T@@*p=NULL;free(p);return 0;}", "accept"),
    ("int main(void){@@T@@*p=NULL;free(p);free(p);return 0;}", "accept"),
    ("int main(void){@@T@@*p=(@@T@@*)NULL;free(p);return 0;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;@@T@@*q=p;free(q);"
     "return x==@@V@@?0:1;}", "accept"),
    ("void f(@@T@@*p){free(p);}int main(void){@@T@@*p=NULL;f(p);return 0;}", "accept"),
    ("int main(void){@@T@@*p=NULL;free(p);p=NULL;free(p);return 0;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;free(p);if(p)return *p;"
     "return x==@@V@@?0:1;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;free(p);p=&x;"
     "return *p==@@V@@?0:1;}", "accept"),
]

# 8. reset to null after free, then guarded use (or naked deref).
P_NULL_AFTER_FREE = [
    ("int main(void){@@T@@*p=malloc(sizeof(@@T@@));if(!p)return 2;"
     "free(p);p=NULL;return p?*p:0;}", "accept"),
    ("int main(void){@@T@@*p=malloc(sizeof(@@T@@));if(!p)return 2;"
     "free(p);p=NULL;if(p)return *p;return 0;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=malloc(sizeof(@@T@@));if(!p)return 2;"
     "free(p);p=NULL;p=&x;return *p==@@V@@?0:1;}", "accept"),
    ("int main(void){@@T@@*p=malloc(sizeof(@@T@@));if(!p)return 2;"
     "free(p);p=NULL;return *p;}", "reject"),
    ("int main(void){@@T@@*p=malloc(sizeof(@@T@@));if(!p)return 2;"
     "free(p);p=NULL;*p=@@V@@;return 0;}", "reject"),
    ("int main(void){@@T@@*p=malloc(sizeof(@@T@@));if(!p)return 2;"
     "free(p);p=NULL;free(p);return 0;}", "accept"),
    ("int main(void){@@T@@*p=calloc(1,sizeof(@@T@@));if(!p)return 2;"
     "free(p);p=NULL;return p?*p:0;}", "accept"),
    ("int main(void){@@T@@*p=malloc(sizeof(@@T@@));if(!p)return 2;"
     "free(p);p=(@@T@@*)NULL;return *p;}", "reject"),
]

# 9. null state through 2-3 hop aliases, struct field, array slot.
P_ALIAS_TWOHOP = [
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;@@T@@*q=p;@@T@@*r=q;return *r;}", "reject"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;@@T@@*q=p;@@T@@*r=q;@@T@@*s=r;"
     "return *s;}", "reject"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=&x;p=NULL;@@T@@*q=p;@@T@@*r=q;"
     "return *r;}", "reject"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;@@T@@*q=p;*q=@@V2@@;return 0;}", "reject"),
    ("struct S{@@T@@*f;};int main(void){struct S s;@@T@@ x=@@V@@;s.f=NULL;"
     "@@T@@*r=s.f;return *r;}", "reject"),
    ("int main(void){@@T@@*a[@@N@@];@@T@@*p=NULL;@@T@@ x=@@V@@;a[@@K@@]=p;"
     "@@T@@*r=a[@@K@@];return *r;}", "reject"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;@@T@@*q=p;@@T@@*r=&x;q=r;p=q;"
     "return *p==@@V@@?0:1;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;@@T@@*q=&x;@@T@@*r=q;"
     "return *r==@@V@@?0:1;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;@@T@@*q=p;q=&x;"
     "return *q==@@V@@?0:1;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;@@T@@*q=p;@@T@@*r=q;"
     "return r?*r:0;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;@@T@@*q=p;p=&x;"
     "return *p==@@V@@?0:1;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;@@T@@*q=p;@@T@@*r=q;"
     "*r=@@V2@@;return 0;}", "reject"),
]

# 10. null state across function calls.
P_INTERPROC = [
    ("int f(@@T@@*p){return *p;}int main(void){@@T@@*p=NULL;return f(p);}", "reject"),
    ("int f(@@T@@*p){return *p;}int main(void){return f((@@T@@*)NULL);}", "reject"),
    ("int h(@@T@@*p){return *p;}int g(@@T@@*p){return h(p);}"
     "int main(void){@@T@@*p=NULL;return g(p);}", "reject"),
    ("void set(@@T@@**pp){*pp=NULL;}int main(void){@@T@@ x=@@V@@;@@T@@*p=&x;"
     "set(&p);return *p;}", "reject"),
    ("int f(@@T@@*p){return p?*p:0;}int main(void){@@T@@*p=NULL;return f(p);}", "accept"),
    ("@@T@@ gv=@@V@@;void set(@@T@@**pp){*pp=&gv;}int main(void){@@T@@*p=NULL;"
     "set(&p);return *p==@@V@@?0:1;}", "accept"),
    ("void f(@@T@@*p){if(p)*p=@@V@@;}int main(void){@@T@@ x=0;@@T@@*p=&x;f(p);"
     "return x==@@V@@?0:1;}", "accept"),
    ("int f(@@T@@*p){@@T@@*q=p;q=NULL;return *q;}int main(void){@@T@@ x=@@V@@;"
     "@@T@@*p=&x;return f(p);}", "reject"),
    ("int f(@@T@@*p){return 0;}int main(void){@@T@@*p=NULL;return f(p);}", "accept"),
    ("int f(@@T@@**pp){return **pp;}int main(void){@@T@@*p=NULL;return f(&p);}", "reject"),
    ("int f(@@T@@*p){return p==0?0:1;}int main(void){@@T@@*p=NULL;return f(p);}", "accept"),
    ("void f(@@T@@*p){*p=@@V@@;}int main(void){@@T@@*p=NULL;f(p);return 0;}", "reject"),
]

# 11. explicit controls that must be rejected.
P_CONTROL_REJECT = [
    ("int main(void){@@T@@*p=NULL;return *p;}", "reject"),
    ("int main(void){@@T@@*p=(@@T@@*)NULL;return *p;}", "reject"),
    ("int main(void){@@T@@*p=0;return *p;}", "reject"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=&x;p=NULL;return *p;}", "reject"),
    ("int main(void){return *(@@T@@*)NULL;}", "reject"),
    ("int main(void){@@T@@*p=NULL;*p=@@V@@;return 0;}", "reject"),
    ("int main(void){@@T@@*p=NULL;return p[@@K@@];}", "reject"),
    ("int main(void){@@T@@*p=NULL;return p[@@KK@@];}", "reject"),
    ("int main(void){@@T@@*p=NULL;@@T@@*q=p;return *q;}", "reject"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=&x;if(p==NULL)return *p;return 0;}", "reject"),
]

# 12. explicit safe controls.
P_CONTROL_ACCEPT = [
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=&x;return *p==@@V@@?0:1;}", "accept"),
    ("int main(void){@@T@@*p=NULL;return p==NULL?0:1;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;if(p)return *p;return x==@@V@@?0:1;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;return p?*p:0;}", "accept"),
    ("int main(void){@@T@@*p=NULL;free(p);return 0;}", "accept"),
    ("int main(void){@@T@@ a[@@N@@]={0};@@T@@*p=&a[@@K2@@];"
     "return p==&a[@@K2@@]?0:1;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=&x;@@T@@*q=p;"
     "return *q==@@V@@?0:1;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=&x;p=NULL;p=&x;"
     "return *p==@@V@@?0:1;}", "accept"),
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;p=&x;"
     "return *p==@@V@@?0:1;}", "accept"),
    # Correct C: `if(!p) p=&x` makes p non-null before the dereference.
    ("int main(void){@@T@@ x=@@V@@;@@T@@*p=NULL;if(!p)p=&x;"
     "return *p==@@V@@?0:1;}", "accept"),
]

FAMILIES = (
    ("null_to_malloc", 10, P_NULL_TO_MALLOC),
    ("null_to_addr", 10, P_NULL_TO_ADDR),
    ("branch_merge", 12, P_BRANCH_MERGE),
    ("loop_reassign", 10, P_LOOP_REASSIGN),
    ("assign_from_q", 10, P_ASSIGN_FROM_Q),
    ("ptr_arith_after_null", 10, P_PTR_ARITH),
    ("free_after_null", 10, P_FREE_AFTER_NULL),
    ("null_after_free", 10, P_NULL_AFTER_FREE),
    ("alias_twohop", 12, P_ALIAS_TWOHOP),
    ("interproc", 12, P_INTERPROC),
    ("control_reject", 10, P_CONTROL_REJECT),
    ("control_accept", 10, P_CONTROL_ACCEPT),
)

# --- deterministic seeded sweeps over every family -------------------------
for _family, _count, _patterns in FAMILIES:
    for _n in range(1, _count + 1):
        _fmt, _expect = _patterns[(_n - 1) % len(_patterns)]
        _body = STD + tpl(_fmt, **params())
        add(_family, tpl("%s_@@I@@" % _family, I=_n), _body, _expect)


# ---------------------------------------------------------------------------
# Seeded mutation pass: re-roll seeded parameters until TARGET distinct
# programs exist.  Families/patterns are chosen deterministically by index so
# worker count cannot affect the corpus.
# ---------------------------------------------------------------------------
def seeded_extra(i):
    family, _count, patterns = FAMILIES[i % len(FAMILIES)]
    fmt, expect = RNG.choice(patterns)
    body = STD + tpl(fmt, **params())
    return family, tpl("x_%s_@@I@@" % family, I=i), body, expect


_i = 0
while len(CASES) < TARGET:
    _family, _name, _body, _expect = seeded_extra(_i)
    add(_family, _name, _body, _expect)
    _i += 1
    if _i > TARGET * 50:
        raise RuntimeError("could not reach TARGET distinct cases")


# ---------------------------------------------------------------------------
# Execution / verification.
# ---------------------------------------------------------------------------
def first_diag(text):
    """Pick one informative diagnostic line from combined stdout/stderr."""
    for line in text.splitlines():
        s = line.strip()
        if not s:
            continue
        if "error:" in s or "warning:" in s or s.startswith("check:"):
            return s[:240]
    lines = [l.strip() for l in text.splitlines() if l.strip()]
    return lines[0][:240] if lines else "(no diagnostics)"


def run_case(case):
    family, name, body, expect = case
    path = CASES_DIR / (name + ".c")
    path.write_text(body)
    modes = {}
    hard = []
    for mode in ("--default", "--strict"):
        try:
            proc = subprocess.run([CC, "check", mode, str(path)],
                                  capture_output=True, text=True, timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            modes[mode] = (False, "TIMEOUT after %ds" % TIMEOUT)
            hard.append("%s: TIMEOUT after %ds" % (mode, TIMEOUT))
            continue
        except OSError as exc:
            modes[mode] = (False, "OS ERROR: %s" % exc)
            hard.append("%s: OS ERROR: %s" % (mode, exc))
            continue
        diag = (proc.stderr + proc.stdout).strip()
        if proc.returncode < 0:
            hard.append("%s: CRASH rc=%d" % (mode, proc.returncode))
        modes[mode] = (proc.returncode != 0, diag)

    d_rej, s_rej = modes["--default"][0], modes["--strict"][0]
    reasons = list(hard)
    false_positive = False
    if expect == "reject":
        if not d_rej:
            reasons.append("unsafe accepted in DEFAULT (false negative)")
        if not s_rej:
            reasons.append("unsafe accepted in STRICT (false negative)")
    elif expect == "strict":
        if not s_rej:
            reasons.append("expected rejection in STRICT; accepted (false negative)")
    else:  # accept
        false_positive = d_rej or s_rej
        if d_rej:
            reasons.append("safe rejected in DEFAULT (false positive)")
        if s_rej:
            reasons.append("safe rejected in STRICT (false positive)")

    result = {
        "name": name, "family": family, "expect": expect,
        "reasons": reasons, "false_positive": false_positive,
        "d_rej": d_rej, "s_rej": s_rej, "modes": modes,
    }
    if not reasons:
        return result

    (FAILS / (name + ".c")).write_text(body)
    log = [
        "case: %s" % name,
        "family: %s" % family,
        "expect: %s" % expect,
        "reasons: %s" % "; ".join(reasons),
        "--default rejected=%s" % d_rej,
        first_diag(modes["--default"][1]),
        "--strict rejected=%s" % s_rej,
        first_diag(modes["--strict"][1]),
        "",
        "===== full --default output =====",
        modes["--default"][1][:4000],
        "",
        "===== full --strict output =====",
        modes["--strict"][1][:4000],
        "",
    ]
    (FAILS / (name + ".diag.txt")).write_text("\n".join(log))
    result["detail"] = (
        "%s [%s] expect=%s reasons=%s | DEFAULT rejected=%s: %s | "
        "STRICT rejected=%s: %s" % (
            name, family, expect, ";".join(reasons),
            d_rej, first_diag(modes["--default"][1]),
            s_rej, first_diag(modes["--strict"][1])))
    return result


def main():
    bodies = [b for _, _, b, _ in CASES]
    unique = len(set(bodies))
    counts = Counter(e for _, _, _, e in CASES)
    families = Counter(f for f, _, _, _ in CASES)
    print("nl_invalidate: %d generated cases (%d distinct), seed=%s, workers=%d, bin=%s"
          % (len(CASES), unique, hex(SEED), WORKERS, CC))
    print("expected outcomes: reject=%d strict=%d accept=%d"
          % (counts["reject"], counts["strict"], counts["accept"]))
    print("families (%d): %s" % (len(families),
          ", ".join("%s=%d" % kv for kv in sorted(families.items()))))
    if unique != len(CASES):
        print("WARNING: %d duplicate program bodies generated" % (len(CASES) - unique))

    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        results = list(pool.map(run_case, CASES))

    bad = [r for r in results if r["reasons"]]
    mismatches = sorted(r["detail"] for r in bad)
    mismatch_names = sorted(r["name"] for r in bad)
    false_positives = sorted(r["name"] for r in results if r["false_positive"])
    false_negatives = sum(1 for r in bad if r["expect"] != "accept")
    hard = sum(1 for r in results for reason in r["reasons"]
               if "TIMEOUT" in reason or "OS ERROR" in reason or "CRASH" in reason)

    print("results: %d cases, %d mismatches (%d false negatives, %d false positives, %d hard errors)"
          % (len(CASES), len(mismatches), false_negatives, len(false_positives), hard))
    print("false positives: %d" % len(false_positives))
    if false_positives:
        print("  " + ", ".join(false_positives[:20]))
        if len(false_positives) > 20:
            print("  ... %d more" % (len(false_positives) - 20))
    print("first 10 mismatch names:")
    for r in sorted(bad, key=lambda x: x["name"])[:10]:
        print("  %s: %s" % (r["name"], "; ".join(r["reasons"])))
    if mismatches:
        print("saved %d mismatch fixtures in %s" % (len(mismatches), FAILS))
    print("FINAL: %s (%d cases, %d mismatches, %d false positives)" % (
        "FAIL" if (mismatches or unique != len(CASES)) else "PASS",
        len(CASES), len(mismatches), len(false_positives)))
    return 1 if mismatches or unique != len(CASES) else 0


if __name__ == "__main__":
    sys.exit(main())
