#!/usr/bin/env python3
"""Permanent paired C+/Rust safety-proof runner.

For every scenario this runner WRITES an equivalent C program and Rust program
that perform the *same* unsafe operation, then executes, with a hard 45s timeout
per process and no shell short-circuits:

  c+ check --strict --sanitize=none <c>            (compile-time evidence)
  c+ build --strict --sanitize=none <c> -o <bin>   (build evidence)
  <c-bin>                                          (runtime evidence)
  rustc --edition 2021 -C overflow-checks=on <rs> -o <bin>  (compile evidence)
  <rs-bin>                                         (runtime evidence)

Each result records exact rc and exact stdout/stderr (no `|| true`, no
normalisation of diagnostics), then classifies the pair:

  AGREE_COMPILE  both reject at compile time
  AGREE_TRAP     both compile, both trap deterministically at run time
  CPLUS_STRICTER C+ rejects at compile, rustc accepts (Rust needs `unsafe`)
  RUST_STRICTER  rustc rejects, C+ accepts
  MISMATCH       neither produced a safety event / evidence does not match

A scenario PASSES only when both languages produce a safety event with the
expected diagnostic, OR when the recorded difference is a documented
intentional difference carrying an explicit `why`. Any MISMATCH -> exit 1.

Usage:  CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/rust_proof/run_proof.py
"""
from __future__ import annotations

import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import textwrap
from datetime import datetime, timezone

HERE = pathlib.Path(__file__).resolve().parent
SCEN_DIR = HERE / "scenarios"
TIMEOUT_S = 45
CPLUS_BIN = os.environ.get("CPLUS_BIN", str(HERE.parents[2] / "build/c+"))


def src(s: str) -> str:
    return textwrap.dedent(s).lstrip("\n")


# ---------------------------------------------------------------------------
# Scenario corpus: 14 paired proofs.  Each pair implements the SAME operation;
# no Rust code is derived by translation from C+ internals.
# ---------------------------------------------------------------------------
SCENARIOS = [
    {
        "id": "const_oob",
        "operation": "constant out-of-bounds array read",
        "c_source": src("""
            #include <stdio.h>
            int main(void){int a[4]={0};return a[7];}
        """),
        "rust_source": src("""
            const A: [i32;4] = [0;4]; const X: i32 = A[7];
            fn main(){ println!("{}", X); }
        """),
        "rust_requires_unsafe": False,
        "expected_classification": "AGREE_COMPILE",
        "expected_c_diag": "[CPLUS-SAFE-BOUNDS] constant array index 7",
        "expected_rust_code": "E0080",
        "expected_rust_msg": "index out of bounds",
    },
    {
        "id": "dyn_oob",
        "operation": "runtime-determined out-of-bounds array read",
        "c_source": src("""
            #include <stdlib.h>
            int main(void){int a[4]={0};const char*s=getenv("PROOF_IDX");
                           int i=s?atoi(s):100;return a[i];}
        """),
        "rust_source": src("""
            fn main(){ let a=[0i32;4];
                let i:usize=std::env::args().count()+99;
                println!("{}",a[i]); }
        """),
        "rust_requires_unsafe": False,
        "expected_classification": "AGREE_TRAP",
        "expected_c_runtime_diag": "index 100 is outside the bounds of 'a' (length 4)",
        "expected_rust_msg": "index out of bounds",
    },
    {
        "id": "uaf",
        "operation": "use-after-free: dereference a released allocation",
        "c_source": src("""
            #include <stdlib.h>
            int main(void){int*p=malloc(4);if(!p)return 1;free(p);return *p;}
        """),
        "rust_source": src("""
            // Equivalent operation requires unsafe: Box dropped, raw ptr kept.
            fn main(){ let mut b=Box::new(1i32);
                let p=&mut *b as *mut i32; drop(b);
                let v=unsafe{*p}; println!("{}",v); }
        """),
        "rust_requires_unsafe": True,
        "expected_classification": "CPLUS_STRICTER",
        "expected_c_diag": "[CPLUS-SAFE-LIFETIME] use-after-free",
        "documented_difference": True,
        "why": ("C+ emits the named [CPLUS-SAFE-LIFETIME] use-after-free diagnostic at "
                "compile time. Rust has no safe spelling for this operation: the "
                "equivalent needs `unsafe` raw pointers and is undefined behaviour "
                "with no guaranteed deterministic trap (observed here: rustc accepts "
                "and the binary ran to completion). This is an intentional "
                "difference in enforcement point, not an agreement."),
    },
    {
        "id": "double_free",
        "operation": "double release of one owner",
        "c_source": src("""
            #include "cplus/cplus.h"
            int main(void){managed int *p=cplus_new(int);*p=1;
                           cplus_release(p);cplus_release(p);return 0;}
        """),
        "rust_source": src("""
            // Equivalent operation requires unsafe: double Box::from_raw.
            fn main(){ let p=Box::into_raw(Box::new(1i32));
                unsafe{ drop(Box::from_raw(p)); drop(Box::from_raw(p)); } }
        """),
        "rust_requires_unsafe": True,
        "expected_classification": "CPLUS_STRICTER",
        "expected_c_diag": "[CPLUS-OWNER] double free of 'p'",
        "documented_difference": True,
        "why": ("C+ emits the named [CPLUS-OWNER] double free diagnostic at compile "
                "time. The Rust equivalent of the same operation requires `unsafe` "
                "raw pointers (safe Rust rejects it as a move error, which would not "
                "be the same operation). rustc therefore accepts; the allocator "
                "traps at runtime instead. Intentional difference: compile-time "
                "named diagnostic vs runtime UB trap."),
    },
    {
        "id": "null_deref",
        "operation": "null pointer dereference",
        "c_source": src("""
            int main(void){int*p=0;return *p;}
        """),
        "rust_source": src("""
            // Equivalent operation requires unsafe: raw null pointer dereference.
            fn main(){ let p: *const i32 = std::ptr::null();
                let v=unsafe{*p}; println!("{}",v); }
        """),
        "rust_requires_unsafe": True,
        "expected_classification": "CPLUS_STRICTER",
        "expected_c_diag": "null pointer dereference",
        "documented_difference": True,
        "why": ("C+ emits the named [CPLUS-SAFE-LIFETIME] null pointer dereference "
                "diagnostic at compile time. A literal null dereference has no safe "
                "Rust spelling; the equivalent needs `unsafe`, so rustc accepts and "
                "the trap happens at runtime (panic/abort). Intentional difference "
                "in enforcement point; not faked as agreement."),
    },
    {
        "id": "signed_overflow",
        "operation": "signed integer overflow at i32::MAX + 1",
        "c_source": src("""
            int main(void){int x=2147483647;volatile int y=1;return x+y;}
        """),
        "rust_source": src("""
            fn main(){ let x=i32::MAX; let y=1i32; println!("{}", x+y); }
        """),
        "rust_requires_unsafe": False,
        "expected_classification": "AGREE_COMPILE",
        "expected_c_diag": "[CPLUS-ARITH] signed integer overflow in addition",
        "expected_rust_msg": "this arithmetic operation will overflow",
    },
    {
        "id": "div_zero",
        "operation": "integer division by zero",
        "c_source": src("""
            int main(void){int z=0;volatile int x=10;return x/z;}
        """),
        "rust_source": src("""
            fn main(){ let z=0i32; let x=10i32; println!("{}", x/z); }
        """),
        "rust_requires_unsafe": False,
        "expected_classification": "AGREE_COMPILE",
        "expected_c_diag": "[CPLUS-ARITH] division by zero",
        "expected_rust_msg": "attempt to divide",
    },
    {
        "id": "invalid_shift",
        "operation": "shift count greater than operand width",
        "c_source": src("""
            #include <stdint.h>
            int main(void){uint32_t x=1;uint32_t bad=x<<32;return (int)bad;}
        """),
        "rust_source": src("""
            fn main(){ let x=1u32; let bad = x << 32u32; println!("{}",bad); }
        """),
        "rust_requires_unsafe": False,
        "expected_classification": "AGREE_COMPILE",
        "expected_c_diag": "[CPLUS-FFI] shift count 32 is not less than the width 32",
        "expected_rust_msg": "attempt to shift left by `32_u32`",
    },
    {
        "id": "uninit_read",
        "operation": "read of an uninitialised local",
        "c_source": src("""
            int main(void){int x;int y=x;return y;}
        """),
        "rust_source": src("""
            fn main(){ let x:i32; let y=x; println!("{}",y); }
        """),
        "rust_requires_unsafe": False,
        "expected_classification": "AGREE_COMPILE",
        "expected_c_diag": "variable 'x' is uninitialized when used here",
        "expected_rust_code": "E0381",
    },
    {
        "id": "use_after_move",
        "operation": "use of a moved-from owner",
        "c_source": src("""
            #include "cplus/cplus.h"
            int main(void){managed int*p=cplus_new(int);
                managed int*q=cplus_move(p);(void)q;return *p;}
        """),
        "rust_source": src("""
            fn main(){ let s=String::from("x"); let t=s;
                println!("{}", s); let _=t; }
        """),
        "rust_requires_unsafe": False,
        "expected_classification": "AGREE_COMPILE",
        "expected_c_diag": "use-after-move: use of moved-from resource 'p'",
        "expected_rust_code": "E0382",
    },
    {
        "id": "exclusive_borrow",
        "operation": "exclusive mutable borrow alias: f(&x,&x)",
        "c_source": src("""
            void f(int*a,int*b){*a=1;*b=2;}
            int main(void){int x=0;f(&x,&x);return x;}
        """),
        "rust_source": src("""
            fn f(a:&mut i32,b:&mut i32){*a=1;*b=2;}
            fn main(){ let mut x=0; f(&mut x,&mut x); }
        """),
        "rust_requires_unsafe": False,
        "expected_classification": "AGREE_COMPILE",
        "expected_c_diag": "[CPLUS-BORROW] exclusive mutable borrow violated",
        "expected_rust_code": "E0499",
    },
    {
        "id": "dangling_stack",
        "operation": "return of a pointer/reference to a stack local",
        "c_source": src("""
            int *f(void){int x=1;return &x;}
            int main(void){int*p=f();return *p;}
        """),
        "rust_source": src("""
            fn f() -> &'static i32 { let x=1; &x }
            fn main(){ println!("{}", *f()); }
        """),
        "rust_requires_unsafe": False,
        "expected_classification": "AGREE_COMPILE",
        "expected_c_diag": "address of stack memory associated with local variable 'x' returned",
        "expected_rust_code": "E0515",
    },
    {
        "id": "format_string",
        "operation": "non-literal format string passed to printf/println",
        "c_source": src("""
            #include <stdio.h>
            int main(void){char s[16];if(fgets(s,sizeof s,stdin))printf(s);return 0;}
        """),
        "rust_source": src("""
            fn main(){ let s=std::env::args().nth(1).unwrap_or_default();
                println!(s); }
        """),
        "rust_requires_unsafe": False,
        "expected_classification": "AGREE_COMPILE",
        "expected_c_diag": "[CPLUS-FFI] format string is not a string literal",
        "expected_rust_msg": "format argument must be a string literal",
    },
    {
        "id": "enum_switch",
        "operation": "non-exhaustive switch/match over an enum (C+ -Werror=switch)",
        "c_source": src("""
            enum E{A,B,C};
            int f(enum E e){switch(e){case A:return 1;case B:return 2;}return 0;}
            int main(void){return f(C);}
        """),
        "rust_source": src("""
            enum E { A, B, C }
            fn f(e: E) -> i32 { match e { E::A => 1, E::B => 2 } }
            fn main(){ println!("{}", f(E::C)); }
        """),
        "rust_requires_unsafe": False,
        "expected_classification": "AGREE_COMPILE",
        "expected_c_diag": "enumeration value 'C' not handled in switch [-Werror,-Wswitch]",
        "expected_rust_code": "E0004",
    },
]


def run(cmd: list[str], *, cwd=None, env=None, input_text=None) -> dict:
    """Run one command with a hard timeout; never mask the return code."""
    rec = {"cmd": cmd, "rc": None, "stdout": "", "stderr": "",
           "timeout": False, "error": None}
    try:
        p = subprocess.run(cmd, capture_output=True, text=True,
                           timeout=TIMEOUT_S, cwd=cwd, env=env,
                           input=input_text)
        rec.update(rc=p.returncode, stdout=p.stdout, stderr=p.stderr)
    except subprocess.TimeoutExpired as e:
        rec.update(rc="TIMEOUT", timeout=True,
                   stdout=e.stdout.decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or ""),
                   stderr=e.stderr.decode(errors="replace") if isinstance(e.stderr, bytes) else (e.stderr or ""))
    except OSError as e:
        rec.update(rc="ERROR", error=str(e))
    return rec


def rust_codes(text: str) -> list[str]:
    return re.findall(r"error\[(E\d+)\]", text)


def first_error_line(text: str) -> str:
    for line in text.splitlines():
        if line.startswith("error") or "panicked at" in line:
            return line
    return ""


def panic_excerpt(text: str) -> str:
    lines = [ln for ln in text.splitlines() if ln.strip()]
    for i, ln in enumerate(lines):
        if "panicked at" in ln:
            return "\n".join(lines[i:i + 2])
    return ""


def classify(c: dict, r: dict) -> str:
    c_rejected = (c["check"]["rc"] not in (0, None)) or (c["build"]["rc"] not in (0, None))
    r_rejected = r["compile"]["rc"] not in (0, None)
    c_trapped = c["runtime"]["rc"] is not None and c["runtime"]["rc"] != 0
    r_trapped = r["runtime"]["rc"] is not None and r["runtime"]["rc"] != 0
    if c_rejected and r_rejected:
        return "AGREE_COMPILE"
    if (not c_rejected) and (not r_rejected) and c_trapped and r_trapped:
        return "AGREE_TRAP"
    if c_rejected and not r_rejected:
        return "CPLUS_STRICTER"
    if r_rejected and not c_rejected:
        return "RUST_STRICTER"
    return "MISMATCH"


def evaluate(sc: dict, result: dict) -> tuple[bool, list[str]]:
    """Return (pass, reasons). Evidence must match the declared expectation."""
    reasons: list[str] = []
    c, r, observed = result["c"]["raw"], result["rust"]["raw"], result["classification"]
    exp = sc["expected_classification"]

    if observed != exp:
        reasons.append(f"classification {observed} != expected {exp}")
        return False, reasons
    reasons.append(f"classification {observed} OK")

    c_diag = c["check"]["stderr"] + c["build"]["stderr"]
    c_rt = c["runtime"]["stderr"] + c["runtime"]["stdout"]
    r_comp = r["compile"]["stderr"] + r["compile"]["stdout"]
    r_rt = r["runtime"]["stderr"] + r["runtime"]["stdout"]

    def need(hay: str, needle: str, label: str):
        if needle and needle not in hay:
            reasons.append(f"MISSING {label}: {needle!r}")
            return False
        if needle:
            reasons.append(f"found {label}: {needle!r}")
        return True

    ok = True
    if exp == "AGREE_COMPILE":
        ok &= need(c_diag, sc.get("expected_c_diag", ""), "C+ compile diagnostic")
        ok &= need(r_comp, sc.get("expected_rust_code", ""), "rustc error code")
        ok &= need(r_comp, sc.get("expected_rust_msg", ""), "rustc message")
    elif exp == "AGREE_TRAP":
        ok &= need(c_rt, sc.get("expected_c_runtime_diag", ""), "C+ runtime diagnostic")
        ok &= need(r_rt, sc.get("expected_rust_msg", ""), "Rust panic")
    elif exp == "CPLUS_STRICTER":
        ok &= need(c_diag, sc.get("expected_c_diag", ""), "C+ compile diagnostic")
        if sc.get("rust_requires_unsafe"):
            ok &= need(sc["rust_source"], "unsafe", "Rust unsafe marker")
        if not sc.get("documented_difference") or not sc.get("why"):
            reasons.append("CPLUS_STRICTER needs documented_difference + explicit why")
            ok = False
    elif exp == "RUST_STRICTER":
        ok &= need(r_comp, sc.get("expected_rust_code", ""), "rustc error code")

    # Safety-event sanity: a PASS must have a C+ event of some kind.
    c_event = (c["check"]["rc"] not in (0, None)) or (c["build"]["rc"] not in (0, None)) or (
        c["runtime"]["rc"] is not None and c["runtime"]["rc"] != 0)
    if not c_event:
        reasons.append("no C+ safety event (compile reject or runtime trap)")
        ok = False
    return ok, reasons


def main() -> int:
    SCEN_DIR.mkdir(parents=True, exist_ok=True)
    results = []
    env = os.environ.copy()
    env["PROOF_IDX"] = "100"  # deterministic runtime index for dyn_oob

    for sc in SCENARIOS:
        sid = sc["id"]
        cf = SCEN_DIR / f"{sid}.c"
        rf = SCEN_DIR / f"{sid}.rs"
        cf.write_text(sc["c_source"])
        rf.write_text(sc["rust_source"])

        with tempfile.TemporaryDirectory(prefix=f"rust-proof-{sid}-") as td:
            cbin = str(pathlib.Path(td) / f"{sid}.c.bin")
            rbin = str(pathlib.Path(td) / f"{sid}.rs.bin")

            c_check = run([CPLUS_BIN, "check", "--strict", "--sanitize=none", str(cf)],
                          cwd=str(HERE), env=env)
            c_build = run([CPLUS_BIN, "build", "--strict", "--sanitize=none", str(cf),
                           "-o", cbin], cwd=str(HERE), env=env)
            c_runtime = (run([cbin], cwd=str(HERE), env=env, input_text="")
                         if c_build["rc"] == 0 else
                         {"cmd": [], "rc": None, "stdout": "", "stderr": "",
                          "timeout": False, "error": None})

            r_compile = run(["rustc", "--edition", "2021", "-C", "overflow-checks=on",
                             str(rf), "-o", rbin], cwd=str(HERE), env=env)
            r_runtime = (run([rbin], cwd=str(HERE), env=env, input_text="")
                         if r_compile["rc"] == 0 else
                         {"cmd": [], "rc": None, "stdout": "", "stderr": "",
                          "timeout": False, "error": None})

        result = {
            "scenario": sid,
            "operation": sc["operation"],
            "c_source_path": str(cf),
            "rust_source_path": str(rf),
            "c_source": sc["c_source"],
            "rust_source": sc["rust_source"],
            "rust_requires_unsafe": sc["rust_requires_unsafe"],
            "c": {
                "check_rc": c_check["rc"],
                "check_diagnostic": c_check["stderr"] + c_check["stdout"],
                "build_rc": c_build["rc"],
                "build_diagnostic": c_build["stderr"] + c_build["stdout"],
                "runtime_rc": c_runtime["rc"],
                "runtime_diagnostic": c_runtime["stderr"] + c_runtime["stdout"],
                "runtime_stdout": c_runtime["stdout"],
                "runtime_stderr": c_runtime["stderr"],
                "raw": {"check": c_check, "build": c_build, "runtime": c_runtime},
            },
            "rust": {
                "compile_rc": r_compile["rc"],
                "compile_error_codes": rust_codes(r_compile["stderr"]),
                "compile_error_message": first_error_line(r_compile["stderr"]),
                "compile_diagnostic": r_compile["stderr"] + r_compile["stdout"],
                "runtime_rc": r_runtime["rc"],
                "runtime_panic": panic_excerpt(r_runtime["stderr"]),
                "runtime_diagnostic": r_runtime["stderr"] + r_runtime["stdout"],
                "runtime_stdout": r_runtime["stdout"],
                "runtime_stderr": r_runtime["stderr"],
                "raw": {"compile": r_compile, "runtime": r_runtime},
            },
        }
        result["classification"] = classify(result["c"]["raw"], result["rust"]["raw"])
        result["expected_classification"] = exp = sc["expected_classification"]
        result["documented_difference"] = bool(sc.get("documented_difference"))
        result["why"] = sc.get("why", "")
        passed, reasons = evaluate(sc, result)
        result["pass"] = passed
        result["reasons"] = reasons
        results.append(result)

    counts = {}
    for res in results:
        counts[res["classification"]] = counts.get(res["classification"], 0) + 1
    passed_n = sum(1 for r in results if r["pass"])
    mismatches = [r for r in results if r["classification"] == "MISMATCH" or not r["pass"]]

    rustc_v = run(["rustc", "--version"])
    cplus_v = run([CPLUS_BIN, "--version"])
    summary = {
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "cplus_bin": CPLUS_BIN,
        "cplus_version": (cplus_v["stdout"] + cplus_v["stderr"]).strip(),
        "rustc_version": (rustc_v["stdout"] + rustc_v["stderr"]).strip(),
        "timeout_seconds": TIMEOUT_S,
        "commands": {
            "c_check": "c+ check --strict --sanitize=none <c>",
            "c_build": "c+ build --strict --sanitize=none <c> -o <bin>",
            "c_runtime": "<c-bin>",
            "rust_compile": "rustc --edition 2021 -C overflow-checks=on <rs> -o <bin>",
            "rust_runtime": "<rs-bin>",
        },
        "counts": {
            "scenarios": len(results),
            "passed": passed_n,
            "failed": len(mismatches),
            "by_classification": counts,
        },
        "mismatch_paths": [r["scenario"] for r in mismatches],
        "results": results,
    }

    (HERE / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    lines = [
        "PAIRED C+/RUST SAFETY PROOF RUNNER (permanent suite)",
        f"c+        : {CPLUS_BIN}",
        f"c+ version: {summary['cplus_version']}",
        f"rustc     : {summary['rustc_version']}",
        f"timeout   : {TIMEOUT_S}s   (hard, no || true)",
        "",
        f"COUNTS: scenarios={len(results)} passed={passed_n} failed={len(mismatches)}",
        "BY CLASSIFICATION: " + ", ".join(f"{k}={v}" for k, v in sorted(counts.items())),
        "",
        f"=== MISMATCH / FAIL ({len(mismatches)}) ===",
    ]
    if not mismatches:
        lines.append("  (none)")
    for r in mismatches:
        lines.append(f"  {r['scenario']}: observed={r['classification']} "
                     f"expected={r['expected_classification']}")
        lines.append(f"    C source  : {r['c_source_path']}")
        lines.append(f"    Rust source: {r['rust_source_path']}")
        for reason in r["reasons"]:
            lines.append(f"    - {reason}")
    lines.append("")
    lines.append("=== PER-SCENARIO ===")
    for r in results:
        lines.append(f"[{r['pass'] and 'PASS' or 'FAIL'}] {r['scenario']:<18} "
                     f"{r['classification']:<16} {r['operation']}")
        lines.append(f"    C   check_rc={r['c']['check_rc']} build_rc={r['c']['build_rc']} "
                     f"runtime_rc={r['c']['runtime_rc']}")
        c_all = (r["c"]["check_diagnostic"] + r["c"]["build_diagnostic"] +
                 r["c"]["runtime_diagnostic"])
        first = next((ln for ln in c_all.splitlines()
                      if "error" in ln.lower() or "C+ error" in ln), "")
        if not first:
            first = next((ln for ln in c_all.splitlines() if ln.strip()), "")
        lines.append(f"    C   diag: {first}")
        lines.append(f"    Rust compile_rc={r['rust']['compile_rc']} "
                     f"codes={r['rust']['compile_error_codes']} "
                     f"runtime_rc={r['rust']['runtime_rc']}")
        rmsg = (r["rust"]["compile_error_message"] or
                r["rust"]["runtime_panic"].replace("\n", " | "))
        if not rmsg:
            rmsg = next((ln for ln in r["rust"]["runtime_diagnostic"].splitlines()
                         if ln.strip()), "") or f"(no text; runtime_rc={r['rust']['runtime_rc']})"
        lines.append(f"    Rust diag: {rmsg}")
        if r["why"]:
            lines.append(f"    why: {r['why']}")
    (HERE / "summary.txt").write_text("\n".join(lines) + "\n")

    print("\n".join(lines[:12]))
    if mismatches:
        print("MISMATCH PATHS:")
        for r in mismatches:
            print(f"  {r['c_source_path']}  {r['rust_source_path']}")
    return 1 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
