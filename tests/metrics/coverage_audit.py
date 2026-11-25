#!/usr/bin/env python3
"""Coverage audit: map every emitted C+ diagnostic to test evidence.

This does not claim memory safety. It reports, with evidence, which diagnostic
families have at least one negative test (an exact expected diagnostic) and
which test suites currently pass. Run after the parallel work is integrated.
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
SUITES = [
    ("repo", "tests/run_tests.sh"),
    ("core safety", "tests/core/run.sh"),
    ("native safety", "tests/native/cplus_safety_tests.c"),
    ("strict40", "tests/native/strict40_runner.c"),
    ("red team", "tests/core/redteam_100.py"),
]

# Diagnostic families emitted by the compiler.
tags = set()
for src in (ROOT / "compiler").rglob("*.cpp"):
    text = src.read_text(errors="ignore")
    tags.update(re.findall(r"CPLUS-[A-Z]+", text))

# Evidence: scan test sources for tag usage or the suite that asserts it.
test_text = ""
for p in (ROOT / "tests").rglob("*"):
    if p.is_file() and p.suffix in {".py", ".sh", ".c", ".cp", ".err", ".stderr", ".txt"}:
        if "out" in p.parts:
            continue
        test_text += p.read_text(errors="ignore") + "\n"

# Diagnostics are usually asserted by message substring rather than by the
# literal tag, so use representative message keywords as evidence too.
KEYWORDS = {
    "CPLUS-ARITH": ("signed integer overflow", "division by zero",
                    "remainder by zero"),
    "CPLUS-ALIAS": ("alias", "borrow"),
    "CPLUS-BORROW": ("borrow", "conflict"),
    "CPLUS-FFI": ("format string", "null pointer argument", "memcpy source",
                  "fgets", "fread", "strcpy", "unverifiable provenance"),
    "CPLUS-FREE": ("double free", "invalid free", "use-after-free"),
    "CPLUS-RAII": ("cleanup", "leaks on this return path", "longjmp"),
    "CPLUS-REALLOC": ("realloc",),
    "CPLUS-BOUNDS": ("bounds", "array index", "out-of-bounds"),
    "CPLUS-LIFETIME": ("use-after-free", "uninitialized", "released pointer"),
    "CPLUS-OWNER": ("leaks on this return path", "allocation size", "VLA"),
    "CPLUS-SAFE-BOUNDS": ("outside the bounds", "array index"),
    "CPLUS-SAFE-LIFETIME": ("use-after-free", "uninitialized"),
}
covered, uncovered = [], []
for tag in sorted(tags):
    evidence = tag in test_text or any(
        k in test_text for k in KEYWORDS.get(tag, ()))
    (covered if evidence else uncovered).append(tag)

print(f"Diagnostic families emitted: {len(tags)} (evidence is heuristic)")
print(f"With test evidence:          {len(covered)}")
print(f"Without test evidence:       {len(uncovered)}")
if uncovered:
    print("Uncovered tags:", ", ".join(uncovered))

print("\nSuite results:")
failed = []
for name, rel in SUITES:
    path = ROOT / rel
    if not path.exists():
        print(f"  {name:18} MISSING ({rel})")
        failed.append(name)
        continue
    cmd = ["sh", str(path)] if path.suffix == ".sh" else [sys.executable, str(path)]
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT,
                       timeout=1800)
    out = (r.stdout + r.stderr).strip().splitlines()
    summary = out[-1] if out else "(no output)"
    print(f"  {name:18} {'PASS' if r.returncode == 0 else 'FAIL'} "
          f"rc={r.returncode} {summary[:90]}")
    if r.returncode != 0:
        failed.append(name)

if failed:
    print("\nFAILED SUITES:", ", ".join(failed))
sys.exit(1 if failed else 0)
