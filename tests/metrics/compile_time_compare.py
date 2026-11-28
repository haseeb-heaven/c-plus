#!/usr/bin/env python3
"""Aggregate compile-time safety evidence across all C+ scenario corpora."""
from pathlib import Path
import re

ROOT = Path(__file__).parents[2]
OUT = ROOT / "tests/out"
rows = []

def add(name, report, pattern=None):
    p = OUT / report
    if not p.exists():
        rows.append((name, "no report"))
        return
    first = p.read_text().splitlines()[0]
    rows.append((name, first))

add("bugs51-97 corpus", "core/bugs51_97/report.txt")
add("runtime-crash corpus", "core/runtime_crashes/report.txt")
add("function-pointer bounds", "core/fnptr_bounds/report.txt")
add("C+ vs Rust paired", "core/rust_compare/report.txt")
add("sanitizer diagnostics", "core/sanitizer/report.txt")
add("contract suite", "core/lib_contracts/report.txt")
def add_metrics():
    import json
    p = OUT / "metrics/report.json"
    if not p.exists():
        return
    d = json.loads(p.read_text())
    safe = d.get("safe_corpus", {})
    bugs = d.get("bug_corpus", {})
    print(f"  {'measure.py':28} safe {safe.get('cplus_accepted_strict')}/"
          f"{safe.get('total')} STRICT; FPs "
          f"{safe.get('false_positives_strict')}; bug detection "
          f"{bugs.get('detected_strict')}/{bugs.get('total')} STRICT")

print("Compile-time safety evidence (C+):")
for name, first in rows:
    print(f"  {name:28} {first[:120]}")
add_metrics()
print("\nRust comparison is limited to the 20 paired scenarios in "
      "tests/safety/rust_compare/; other corpora have no Rust equivalent.")
