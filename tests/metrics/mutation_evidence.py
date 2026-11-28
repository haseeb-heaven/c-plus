#!/usr/bin/env python3
"""Mutation-based safety evidence (NOT a proof of absence of bugs).

For each safe-corpus program we apply mechanical, truth-labelled mutations
that are known to introduce an unsafe operation. The detector must reject the
mutant with a C+ diagnostic; mutants that are accepted are reported as false
negatives for the applicable mutation family. The original program must stay
accepted (guards against false positives).
"""
import os
import pathlib
import random
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/metrics/mutation"
OUT.mkdir(parents=True, exist_ok=True)
ENV = dict(os.environ)
ENV["CPLUS_CLANG"] = os.environ.get("CPLUS_CLANG", "/usr/bin/clang")

SOURCES = sorted((ROOT / "tests/metrics/safe_corpus").glob("*.c")) + \
          sorted((ROOT / "tests/core/positive").glob("*.c"))


def mutations(src):
    """Yield (family, mutated_source) pairs with a known unsafe truth label."""
    # 1. zero-size allocation: always a C+ STRICT diagnostic.
    for m in re.finditer(r"\bmalloc\s*\(\s*([0-9]+)\s*\)", src):
        if m.group(1) == "0":
            continue
        yield ("zero_size_alloc",
               src[:m.start()] + "malloc(0)" + src[m.end():])
        break
    # 2. delete a free(p) statement: leak on a straight-line path.
    m = re.search(r"^\s*free\s*\([^;]+\)\s*;.*$", src, re.M)
    if m:
        yield ("missing_free", src[:m.start()] + "\n" + src[m.end():])
    # 3. remove a null-check guard: possible null dereference.
    m = re.search(r"if\s*\(\s*!(\w+)\s*\)\s*(return[^;]*;)", src)
    if m:
        yield ("removed_null_guard",
               src[:m.start()] + m.group(2) + src[m.end():])
    # 4. halve a fixed array extent: indexed writes may exceed it.
    m = re.search(r"\[\s*([4-9][0-9]*)\s*\]", src)
    if m:
        old = int(m.group(1))
        new = max(1, old // 2)
        yield ("shrunk_array",
               src[:m.start()] + f"[{new}]" + src[m.end():])


results = []
mutants = 0
for path in SOURCES:
    try:
        src = path.read_text()
    except OSError:
        continue
    p = OUT / "original.c"
    p.write_text(src)
    base = subprocess.run([CC, "check", "--strict", str(p)], capture_output=True,
                          text=True, env=ENV, timeout=60)
    if base.returncode != 0:
        results.append((path.name, "original", False, "accepted",
                        "baseline rejected"))
        continue
    for family, mutated in mutations(src):
        mutants += 1
        p.write_text(mutated)
        r = subprocess.run([CC, "check", "--strict", str(p)],
                           capture_output=True, text=True, env=ENV, timeout=60)
        diag = r.stderr + r.stdout
        detected = r.returncode != 0 and "CPLUS-" in diag
        results.append((path.name, family, detected, "CPLUS diagnostic",
                        diag.strip().splitlines()[0][:90] if diag else "accepted"))

detected = sum(1 for _, _, ok, _, _ in results if ok)
by_family = {}
for _, family, ok, _, _ in results:
    if family == "original":
        continue
    d, t = by_family.get(family, (0, 0))
    by_family[family] = (d + int(ok), t + 1)

lines = [f"mutation evidence: {detected}/{len(results)} labelled cases detected "
         f"({mutants} mutants from {len(SOURCES)} safe programs)"]
for family, (d, t) in sorted(by_family.items()):
    lines.append(f"  {family:22} {d}/{t} detected")
lines.append("This measures detection on mechanical mutants; it is NOT proof "
             "that untested bugs are absent.")
(OUT / "report.txt").write_text("\n".join(lines) + "\n")
print("\n".join(lines))
sys.exit(0)
