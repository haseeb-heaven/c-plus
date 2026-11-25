#!/usr/bin/env python3
"""Deterministic seeded parallel generator: >=100 distinct valid C programs
covering NULL propagation through FUNCTIONS ("nl_interproc").

Every case is a small, self-contained C translation unit generated
programmatically from a parameter sweep plus a seeded RNG (FUZZ_SEED,
default 0x4E4C49 == "NLI").  All bodies are distinct (enforced by a
deterministic case marker comment plus a SEEN set); re-running with the same
seed is reproducible.  Cases run in parallel (FUZZ_WORKERS, default 8) with a
per-check timeout (FUZZ_TIMEOUT, default 60s).

Families (12 total):

   1  ret_null          callee returns NULL unconditionally; caller derefs it
   2  ret_branch        callee returns NULL on one branch; caller derefs it
   3  arg_null          NULL passed as an argument, dereferenced in the callee
   4  out_param         NULL written through / passed as an out-parameter
   5  fnptr_ret         pointer-to-function returns NULL; caller derefs result
   6  callback_null_ctx callback invoked with a NULL context it dereferences
   7  nested            NULL threaded through 3-4 nested calls, derefed at leaf
   8  ret_2hop          NULL propagated through two return hops, then derefed
   9  guarded_caller    safe control: caller checks the returned pointer
  10  guarded_callee    safe control: callee checks its pointer argument
  11  factory           factory functions that may fail (return NULL)
  12  checked_ret       safe controls: checked returns across all the above

Declared expectations follow the sibling generators (ag_init_100 /
fuzz_interproc_300):

   reject - proven null dereference: must be rejected in BOTH --default and
            --strict.  Acceptance in either mode is a safety HOLE.
   strict - conditional / unverified nullability: MAY pass --default, but must
            be rejected in --strict (acceptance there is a HOLE).
   accept - valid control (checked return/guard): must pass BOTH modes;
            rejection is a FALSE POSITIVE.

Any mismatch is printed and the generated source plus a full both-mode
diagnostic log is saved under tests/out/core/nl_interproc/failures/.  This is a
differential measurement over the declared subset, not a proof of safety.

Environment:
   FUZZ_SEED    default 0x4E4C49
   FUZZ_WORKERS default 8
   FUZZ_TIMEOUT default 60
   CPLUS_BIN    default <root>/build/c+
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
OUT = ROOT / "tests/out/core/nl_interproc"
CASES_DIR = OUT / "cases"
FAILS = OUT / "failures"
OUT.mkdir(parents=True, exist_ok=True)
CASES_DIR.mkdir(parents=True, exist_ok=True)
FAILS.mkdir(parents=True, exist_ok=True)

WORKERS = int(os.environ.get("FUZZ_WORKERS", "8"))
SEED = int(os.environ.get("FUZZ_SEED", "0x4E4C49"), 0)
TIMEOUT = int(os.environ.get("FUZZ_TIMEOUT", "60"))

CASES = []   # dict(name, body, expect, family)
SEEN = set()  # normalized generated program bodies, used to enforce distinctness
RNG = random.Random(SEED)

NS = range(1, 6)             # parameter sweep (RNG values vary per n)
STD = "#include <stdlib.h>\n"


def tpl(template, **kw):
    """Token templating: avoids C-brace escaping in Python format strings."""
    for key, value in kw.items():
        template = template.replace("@@" + key + "@@", str(value))
    return template


def val():
    """Seeded RNG literal, keeps each generated program distinct."""
    return RNG.randint(1, 97)


def add(family, name, body, expect):
    assert expect in ("reject", "strict", "accept"), (name, expect)
    assert name not in {c["name"] for c in CASES}, name
    # A deterministic, case-specific marker makes every program unique in
    # source text; comments are valid C and do not change behavior.
    normalized = (f"/* nl_interproc seed=0x{SEED:X} case={name} "
                  f"family={family} */\n" + body.strip() + "\n")
    if normalized in SEEN:
        raise ValueError("duplicate generated program for case " + name)
    SEEN.add(normalized)
    CASES.append({"name": name, "body": normalized, "expect": expect,
                  "family": family})


# ---------------------------------------------------------------------------
# 1. ret_null: callee returns NULL unconditionally, caller dereferences it.
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("ret_null", tpl("ret_null_w_@@N@@", N=n),
        tpl("static int *f(void){ return 0; }\n"
            "int main(void){ int *p=f(); *p=@@V@@; return 0; }", V=v), "reject")
    add("ret_null", tpl("ret_null_r_@@N@@", N=n),
        tpl("static int *f(void){ return 0; }\n"
            "int main(void){ int *p=f(); return *p+@@V@@; }", V=v), "reject")
    add("ret_null", tpl("ret_null_s_@@N@@", N=n),
        tpl("static int *f(void){ return 0; }\n"
            "int main(void){ int *p=f(); p[@@N@@]=@@V@@; return 0; }",
            N=n, V=v), "reject")

# ---------------------------------------------------------------------------
# 2. ret_branch: callee returns NULL on one (runtime-selected) branch.
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("ret_branch", tpl("ret_branch_then_@@N@@", N=n),
        tpl("static int cell=@@V@@;\n"
            "static int *f(int c){ if(c) return 0; return &cell; }\n"
            "int main(int argc,char **argv){ (void)argv; int *p=f(argc); "
            "*p=@@V@@; return 0; }", V=v), "strict")
    add("ret_branch", tpl("ret_branch_else_@@N@@", N=n),
        tpl("static int cell=@@V@@;\n"
            "static int *f(int c){ if(c) return &cell; return 0; }\n"
            "int main(int argc,char **argv){ (void)argv; int *p=f(argc); "
            "p[@@N@@]=@@V@@; return 0; }", N=n, V=v), "strict")
    add("ret_branch", tpl("ret_branch_tern_@@N@@", N=n),
        tpl("static int cell=@@V@@;\n"
            "static int *f(int c){ return c ? &cell : 0; }\n"
            "int main(int argc,char **argv){ (void)argv; int *p=f(argc); "
            "*p=@@V@@; return 0; }", V=v), "strict")

# ---------------------------------------------------------------------------
# 3. arg_null: NULL passed as an argument, dereferenced inside the callee.
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("arg_null", tpl("arg_null_w_@@N@@", N=n),
        tpl("static void f(int *p){ *p=@@V@@; }\n"
            "int main(void){ f(0); return 0; }", V=v), "reject")
    add("arg_null", tpl("arg_null_r_@@N@@", N=n),
        tpl("static int f(int *p){ return *p+@@V@@; }\n"
            "int main(void){ return f(0); }", V=v), "reject")
    add("arg_null", tpl("arg_null_s_@@N@@", N=n),
        tpl("static void f(int *p){ p[@@N@@]=@@V@@; }\n"
            "int main(void){ f(0); return 0; }", N=n, V=v), "reject")

# ---------------------------------------------------------------------------
# 4. out_param: NULL written through / supplied as an out-parameter.
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("out_param", tpl("out_setnull_@@N@@", N=n),
        STD + tpl("static void set(int **q){ *q=0; }\n"
                  "int main(void){ int *p=malloc(sizeof(int)); if(!p) return 2; "
                  "set(&p); *p=@@V@@; free(p); return 0; }", V=v), "reject")
    add("out_param", tpl("out_nullptr_@@N@@", N=n),
        tpl("static int cell=@@V@@;\n"
            "static void set(int **q){ *q=&cell; }\n"
            "int main(void){ set(0); return 0; }", V=v), "reject")
    add("out_param", tpl("out_cond_@@N@@", N=n),
        STD + tpl("static void set(int **q,int c){ if(c) *q=0; }\n"
                  "int main(int argc,char **argv){ (void)argv; "
                  "int *p=malloc(sizeof(int)); if(!p) return 2; "
                  "set(&p,argc); *p=@@V@@; free(p); return 0; }", V=v), "strict")

# ---------------------------------------------------------------------------
# 5. fnptr_ret: a function pointer returns NULL, caller derefs the result.
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("fnptr_ret", tpl("fnptr_direct_@@N@@", N=n),
        tpl("static int *retnull(void){ return 0; }\n"
            "int main(void){ int *(*fp)(void)=retnull; int *p=fp(); "
            "*p=@@V@@; return 0; }", V=v), "reject")
    add("fnptr_ret", tpl("fnptr_struct_@@N@@", N=n),
        tpl("struct Ops{ int *(*get)(void); };\n"
            "static int *retnull(void){ return 0; }\n"
            "int main(void){ struct Ops o; o.get=retnull; int *p=o.get(); "
            "p[@@N@@]=@@V@@; return 0; }", N=n, V=v), "reject")

# ---------------------------------------------------------------------------
# 6. callback_null_ctx: callback receives a NULL context it dereferences.
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("callback_null_ctx", tpl("cb_direct_@@N@@", N=n),
        tpl("static int cb(void *ctx){ int *p=(int*)ctx; return *p+@@V@@; }\n"
            "int main(void){ return cb(0); }", V=v), "reject")
    add("callback_null_ctx", tpl("cb_thunk_@@N@@", N=n),
        tpl("typedef int (*cb_t)(void*);\n"
            "static int run(cb_t f,void *ctx){ return f(ctx); }\n"
            "static int cb(void *ctx){ int *p=(int*)ctx; *p=@@V@@; return 0; }\n"
            "int main(void){ return run(cb,0); }", V=v), "reject")
    add("callback_null_ctx", tpl("cb_cond_@@N@@", N=n),
        tpl("static int cell=@@V@@;\n"
            "static int cb(void *ctx){ int *p=(int*)ctx; return *p+@@V@@; }\n"
            "int main(int argc,char **argv){ (void)argv; "
            "void *ctx=argc?&cell:0; return cb(ctx); }", V=v), "strict")

# ---------------------------------------------------------------------------
# 7. nested: NULL threaded through 3-4 calls, dereferenced at the leaf.
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("nested", tpl("nested3_@@N@@", N=n),
        tpl("static void h(int *p){ *p=@@V@@; }\n"
            "static void g(int *p){ h(p); }\n"
            "static void f(void){ g(0); }\n"
            "int main(void){ f(); return 0; }", V=v), "reject")
    add("nested", tpl("nested4_@@N@@", N=n),
        tpl("static void k(int *p){ p[@@N@@]=@@V@@; }\n"
            "static void h(int *p){ k(p); }\n"
            "static void g(int *p){ h(p); }\n"
            "static void f(void){ g(0); }\n"
            "int main(void){ f(); return 0; }", N=n, V=v), "reject")

# ---------------------------------------------------------------------------
# 8. ret_2hop: NULL propagated through two return hops, then dereferenced.
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("ret_2hop", tpl("twohop_const_@@N@@", N=n),
        tpl("static int *h(void){ return 0; }\n"
            "static int *g(void){ return h(); }\n"
            "static int *f(void){ return g(); }\n"
            "int main(void){ int *p=f(); *p=@@V@@; return 0; }", V=v), "reject")
    add("ret_2hop", tpl("twohop_branch_@@N@@", N=n),
        tpl("static int cell=@@V@@;\n"
            "static int *h(int c){ return c?&cell:0; }\n"
            "static int *g(int c){ return h(c); }\n"
            "int main(int argc,char **argv){ (void)argv; int *p=g(argc); "
            "*p=@@V@@; return 0; }", V=v), "strict")

# ---------------------------------------------------------------------------
# 9. guarded_caller: safe control, caller checks the returned pointer.
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("guarded_caller", tpl("gc_ifnot_@@N@@", N=n),
        tpl("static int *f(void){ return 0; }\n"
            "int main(void){ int *p=f(); if(!p) return 2; *p=@@V@@; return 0; }",
            V=v), "accept")
    add("guarded_caller", tpl("gc_block_@@N@@", N=n),
        tpl("static int *f(void){ return 0; }\n"
            "int main(void){ int *p=f(); if(p){ p[@@N@@]=@@V@@; } return 0; }",
            N=n, V=v), "accept")

# ---------------------------------------------------------------------------
# 10. guarded_callee: safe control, callee checks its pointer argument.
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("guarded_callee", tpl("guardc_w_@@N@@", N=n),
        tpl("static void f(int *p){ if(p) *p=@@V@@; }\n"
            "int main(void){ f(0); return 0; }", V=v), "accept")
    add("guarded_callee", tpl("guardc_r_@@N@@", N=n),
        tpl("static int f(int *p){ if(!p) return 0; return *p+@@V@@; }\n"
            "int main(void){ return f(0); }", V=v), "accept")

# ---------------------------------------------------------------------------
# 11. factory: factory functions that may fail (return NULL).
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("factory", tpl("factory_alloc_@@N@@", N=n),
        STD + tpl("static int *factory(void){ return malloc(sizeof(int)); }\n"
                  "int main(void){ int *p=factory(); *p=@@V@@; free(p); "
                  "return 0; }", V=v), "strict")
    add("factory", tpl("factory_branch_@@N@@", N=n),
        tpl("static int cell=@@V@@;\n"
            "static int *factory(int c){ if(c) return 0; return &cell; }\n"
            "int main(int argc,char **argv){ (void)argv; int *p=factory(argc); "
            "*p=@@V@@; return 0; }", V=v), "strict")
    add("factory", tpl("factory_checked_@@N@@", N=n),
        STD + tpl("static int *factory(void){ return malloc(sizeof(int)); }\n"
                  "int main(void){ int *p=factory(); if(!p) return 2; "
                  "*p=@@V@@; free(p); return 0; }", V=v), "accept")

# ---------------------------------------------------------------------------
# 12. checked_ret: safe controls, checked returns across all the above shapes.
# ---------------------------------------------------------------------------
for n in NS:
    v = val()
    add("checked_ret", tpl("checked_chain_@@N@@", N=n),
        tpl("static int *h(void){ return 0; }\n"
            "static int *g(void){ return h(); }\n"
            "int main(void){ int *p=g(); if(p==0) return 2; "
            "*p=@@V@@; return 0; }", V=v), "accept")
    add("checked_ret", tpl("checked_arg_@@N@@", N=n),
        tpl("static void f(int *p){ if(p==0) return; *p=@@V@@; }\n"
            "int main(void){ f(0); return 0; }", V=v), "accept")
    add("checked_ret", tpl("checked_cb_@@N@@", N=n),
        tpl("static int cb(void *ctx){ if(!ctx) return 0; "
            "int *p=(int*)ctx; return *p+@@V@@; }\n"
            "int main(void){ return cb(0); }", V=v), "accept")


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
    name, body, expect, family = (case["name"], case["body"],
                                  case["expect"], case["family"])
    path = CASES_DIR / (name + ".c")
    path.write_text(body)
    modes = {}
    hard = []
    for mode in ("--default", "--strict"):
        try:
            proc = subprocess.run([CC, "check", mode, str(path)],
                                  capture_output=True, text=True, timeout=TIMEOUT)
        except subprocess.TimeoutExpired:
            modes[mode] = (False, "TIMEOUT after %ds" % TIMEOUT, "TIMEOUT")
            hard.append("%s: TIMEOUT after %ds" % (mode, TIMEOUT))
            continue
        except OSError as exc:
            modes[mode] = (False, "OS ERROR: %s" % exc, "OS-ERROR")
            hard.append("%s: OS ERROR: %s" % (mode, exc))
            continue
        diag = (proc.stderr + proc.stdout).strip()
        if proc.returncode < 0:
            hard.append("%s: CRASH rc=%d" % (mode, proc.returncode))
        modes[mode] = (proc.returncode != 0, diag, first_diag(diag))

    d_rej, s_rej = modes["--default"][0], modes["--strict"][0]
    reasons = list(hard)
    if expect == "reject":
        if not d_rej:
            reasons.append("DEFAULT-HOLE")
        if not s_rej:
            reasons.append("STRICT-HOLE")
    elif expect == "strict":
        if not s_rej:
            reasons.append("STRICT-HOLE")
    else:  # accept
        if d_rej:
            reasons.append("DEFAULT-FALSE-POSITIVE")
        if s_rej:
            reasons.append("STRICT-FALSE-POSITIVE")

    if not reasons:
        return family, name, expect, [], modes

    # Save the generated source and a full both-mode diagnostic for each miss.
    (FAILS / (name + ".c")).write_text(body)
    log = [
        "case: %s" % name,
        "family: %s" % family,
        "expect: %s" % expect,
        "reasons: %s" % "; ".join(reasons),
        "--default rejected=%s: %s" % (d_rej, modes["--default"][2]),
        "--strict  rejected=%s: %s" % (s_rej, modes["--strict"][2]),
        "",
        "===== generated source =====",
        body,
        "===== full --default output =====",
        modes["--default"][1][:4000],
        "",
        "===== full --strict output =====",
        modes["--strict"][1][:4000],
        "",
    ]
    (FAILS / (name + ".diag.txt")).write_text("\n".join(log))
    detail = ("%s/%s [expect=%s] %s | DEFAULT rejected=%s: %s | "
              "STRICT rejected=%s: %s" % (
                  family, name, expect, ";".join(reasons),
                  d_rej, modes["--default"][2],
                  s_rej, modes["--strict"][2]))
    return family, name, expect, [detail], modes


def main():
    bodies = [c["body"] for c in CASES]
    unique = len(set(bodies))
    counts = Counter(c["expect"] for c in CASES)
    fams = Counter(c["family"] for c in CASES)

    print("nl_interproc: %d generated cases (%d distinct), seed=0x%X, "
          "workers=%d, bin=%s" % (len(CASES), unique, SEED, WORKERS, CC))
    print("outcomes: reject=%d strict=%d accept=%d"
          % (counts["reject"], counts["strict"], counts["accept"]))
    print("families (%d): %s" % (
        len(fams), ", ".join("%s=%d" % (k, v) for k, v in sorted(fams.items()))))
    if unique != len(CASES):
        print("WARNING: %d duplicate program bodies generated"
              % (len(CASES) - unique))

    mismatches = []
    mismatch_names = []
    holes = 0
    false_positives = 0
    by_family = Counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        for family, name, expect, fails, _modes in pool.map(run_case, CASES):
            if not fails:
                continue
            mismatches.extend(fails)
            mismatch_names.append(name)
            by_family[family] += 1
            if expect == "accept":
                false_positives += 1
            else:
                holes += 1

    print("results: %d cases, %d mismatches (%d safety holes, %d false positives)"
          % (len(CASES), len(mismatches), holes, false_positives))
    if by_family:
        print("mismatches by family: " + ", ".join(
            "%s=%d" % (k, v) for k, v in sorted(by_family.items())))
    print("first 10 mismatch names:")
    for nm in mismatch_names[:10]:
        print("  " + nm)
    if len(mismatch_names) > 10:
        print("  ... %d more (see saved logs in %s)"
              % (len(mismatch_names) - 10, FAILS))
    if mismatches:
        print("saved %d mismatch fixtures in %s" % (len(mismatches), FAILS))
    return 1 if mismatches or unique != len(CASES) else 0


if __name__ == "__main__":
    sys.exit(main())
