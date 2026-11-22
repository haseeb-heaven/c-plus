#!/usr/bin/env python3
"""Side-by-side compile-time safety comparison: C+ vs Rust (safe subset).

Rust is compiled with `--emit=metadata` (no linking, no execution): this
measures compile-time rejection only. Each C scenario must meet its own
expected C+ behaviour; the Rust column is informational evidence, not a
dependency of the test result.
"""
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CDIR = ROOT / "tests/safety/rust_compare/c"
RDIR = ROOT / "tests/safety/rust_compare/rs"
OUT = ROOT / "tests/out/core/rust_compare"
OUT.mkdir(parents=True, exist_ok=True)
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
RUSTC = os.environ.get("RUSTC_BIN", "rustc")
ENV = dict(os.environ)
if os.environ.get("CPLUS_CLANG"):
    ENV["CPLUS_CLANG"] = os.environ["CPLUS_CLANG"]

# name -> (expected C+ diagnostic substring or "" for accept, explanation)
EXPECT = {
    "uaf": ("use-after-free", "Rust: ownership moved, borrow checker rejects"),
    "double_free": ("double free", "Rust: value moved on first drop"),
    "null_deref": ("null pointer dereference", "Rust: Option::unwrap on None caught in const eval"),
    "dangling_stack": ("address of stack", "Rust: cannot return ref to local"),
    "const_oob": ("constant array index 7", "Rust: const eval index check"),
    "dyn_oob": ("", "both: runtime bounds check (C+ check, Rust panic)"),
    "uninit": ("uninitialized", "Rust: definite initialization"),
    "signed_overflow": ("", "C+: UBSan runtime; Rust: const-eval error only in const context"),
    "unsigned_overflow": ("", "both: wrapping is defined"),
    "data_race": ("unsynchronized write", "Rust: closure borrow in thread rejected"),
    "mutable_alias": ("", "C+ accepts aliased ints; Rust rejects two &mut borrows"),
    "bad_format": ("format string is not a string literal", "Rust: format arg must be literal"),
    "format_arg_mismatch": ("more '%' conversions", "Rust: positional count checked"),
    "strcpy_overflow": ("string copy of", "Rust: const-eval copy_from_slice"),
    "leak_on_return": ("", "C+ accepts leak; Rust File has Drop (safe by construction)"),
    "realloc_lost": ("raw realloc return value is ignored", "Rust: Vec manages reallocation"),
    "ptr_oob": ("one past the end", "Rust: const-eval index check"),
    "vla_unbounded": ("variable-length stack allocation", "Rust: no VLA, Vec is heap-checked"),
    "int_to_ptr": ("unverifiable provenance", "Rust: raw pointer deref requires unsafe"),
    "field_uaf": ("use-after-free", "Rust: borrow of s while dropping s rejected"),
}

results = []
for name, (want, note) in EXPECT.items():
    c = subprocess.run([CC, "check", "--strict", str(CDIR / f"{name}.c")],
                       capture_output=True, text=True, env=ENV, timeout=120)
    cdiag = c.stderr + c.stdout
    r = subprocess.run([RUSTC, "--edition", "2021", "--emit=metadata",
                        "-o", str(OUT / f"{name}.rmeta"),
                        str(RDIR / f"{name}.rs")],
                       capture_output=True, text=True, env=ENV, timeout=120)
    rdiag = r.stderr + r.stdout
    if want:
        ok = c.returncode != 0 and want in cdiag
    else:
        ok = c.returncode == 0 and "error:" not in cdiag
    results.append((name, c.returncode != 0, r.returncode != 0, ok,
                    want or "accepted", note))

passed = sum(1 for _, _, _, ok, _, _ in results if ok)
c_count = sum(1 for _, c, _, _, _, _ in results if c)
r_count = sum(1 for _, _, rr, _, _, _ in results if rr)
lines = [
    f"rust-compare suite: {passed}/{len(results)} C+ expectations met",
    f"compile-time rejection: C+ {c_count}/{len(results)}, Rust {r_count}/{len(results)}",
    f"{'scenario':20} {'C+':5} {'Rust':6} {'ok':5} expectation / note",
]
for name, c, rr, ok, want, note in results:
    lines.append(f"{name:20} {'REJECT' if c else 'accept':5} "
                 f"{'REJECT' if rr else 'accept':6} {'ok' if ok else 'FAIL':5} "
                 f"{want} | {note}")
(OUT / "report.txt").write_text("\n".join(lines) + "\n")
print("\n".join(lines[:3]))
if passed != len(results):
    print("\n".join(l for l in lines if "FAIL" in l))
sys.exit(0 if passed == len(results) else 1)
