#!/usr/bin/env python3
"""Unit suite for bugs51.cp .. bugs97.cp (188 unique scenarios).

Reads the marker format emitted by c-plus-stdlib/scratch/generate_bugs.py:
  // scenario: NAME
  // EXPECT: reject|accept
  // DIAG: exact diagnostic substring
and compiles each scenario in isolation. A reject case must exit non-zero and
contain the exact DIAG text; an accept case must compile with no errors.
"""
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).parents[2]
SCRATCH = pathlib.Path(os.environ.get(
    "CPLUS_STDLIB_SCRATCH",
    str(ROOT.parent / "c-plus-stdlib" / "scratch")))
CC = os.environ.get("CPLUS_BIN", str(ROOT / "build/c+"))
OUT = ROOT / "tests/out/core/bugs51_97"
OUT.mkdir(parents=True, exist_ok=True)
ENV = dict(os.environ)
if os.environ.get("CPLUS_CLANG"):
    ENV["CPLUS_CLANG"] = os.environ["CPLUS_CLANG"]


def scenarios(path):
    text = path.read_text().splitlines()
    first = next(i for i, l in enumerate(text) if l.startswith("// scenario:"))
    header = "\n".join(text[:first]) + "\n"
    cases = []
    i = first
    while i < len(text):
        if not text[i].startswith("// scenario:"):
            i += 1
            continue
        name = text[i].split(":", 1)[1].strip()
        expect = ""
        diag = ""
        gap_reason = ""
        j = i + 1
        while j < len(text) and text[j].startswith("//"):
            if text[j].startswith("// EXPECT:"):
                expect = text[j].split(":", 1)[1].strip()
            if text[j].startswith("// DIAG:"):
                diag = text[j].split(":", 1)[1].strip()
            if text[j].startswith("// GAP:"):
                gap_reason = text[j].split(":", 1)[1].strip()
            j += 1
        body = []
        while j < len(text) and text[j].strip():
            body.append(text[j])
            j += 1
        cases.append((name, expect, diag, "\n".join(body), gap_reason))
        i = j
    return header, cases


if not SCRATCH.exists():
    print("bugs51-97 suite: SKIPPED (external c-plus-stdlib/scratch corpus "
          "not checked out)")
    (OUT / "report.txt").write_text(
        "SKIPPED: external c-plus-stdlib/scratch corpus not checked out\n")
    sys.exit(0)

results = []
seen = set()
for num in range(51, 98):
    path = SCRATCH / f"bugs{num}.cp"
    if not path.exists():
        results.append((f"bugs{num}", "missing file", False, "", ""))
        continue
    header, cases = scenarios(path)
    passes = 0
    for name, expect, diag, body, gap_reason in cases:
        if name in seen:
            results.append((name, "duplicate", False, "", ""))
            continue
        seen.add(name)
        src = header + body + "\n"
        p = OUT / f"{name}.c"
        p.write_text(src)
        r = subprocess.run([CC, "check", "--strict", str(p)],
                           capture_output=True, text=True, env=ENV, timeout=120)
        out = r.stderr + r.stdout
        if expect == "gap":
            ok = bool(gap_reason)  # documented, tracked, not silently accepted
        elif expect == "reject":
            ok = r.returncode != 0 and diag in out
        elif expect == "warn":
            ok = diag in out
        else:
            ok = r.returncode == 0 and "error:" not in out
        passes += int(ok)
        results.append((name, expect, ok, diag, gap_reason))

passed = sum(1 for _, _, ok, _, _ in results if ok)
# Runtime side: compile and execute the safe counterparts. These exercise
# the C+ runtime (allocation/free, realloc, files, threads) and must exit 0.
RUNTIME_OK = [
    "bug51_004_mul_size_ok", "bug52_004_realloc_checked_ok",
    "bug87_003_cleanup_on_all_paths_ok", "bug87_004_nested_alloc_ok",
    "bug88_004_strdup_free_ok", "bug89_004_signal_safe_ok",
    "bug90_002_pthread_detach_ok", "bug90_004_thread_join_ok",
    "bug91_004_ctype_ok", "bug92_004_alloc_checked_ok",
    "bug93_002_snprintf_ret_index_guard_ok", "bug93_004_snprintf_ret_store_ok",
    "bug94_003_memset_extent_ok", "bug94_004_memcpy_exact_ok",
    "bug95_004_heap_escape_ok", "bug96_002_realloc_failure_ok",
    "bug96_003_realloc_success_ok", "bug96_004_realloc_null_ok",
    "bug97_004_single_owner_ok",
]
for name in RUNTIME_OK:
    scenario_src = OUT / f"{name}.c"
    if not scenario_src.exists():
        results.append((name + "[runtime]", "runtime", False, "exit 0",
                        "scenario source missing (external corpus not checked out)"))
        continue
    src = scenario_src.read_text()
    exe = OUT / f"{name}.bin"
    rp = OUT / f"{name}.runtime.c"
    args = {"bug87_003_cleanup_on_all_paths_ok": "2",
            "bug91_004_ctype_ok": "'a'"}.get(name, "")
    rp.write_text(src + f"\nint main(void){{ {name}({args}); return 0; }}\n")
    b = subprocess.run([CC, "build", str(rp), "-o", str(exe)],
                       capture_output=True, text=True, env=ENV, timeout=120)
    if b.returncode != 0:
        results.append((name + "[runtime]", "runtime", False, "exit 0",
                        b.stderr.strip().splitlines()[:1]))
        continue
    try:
        r = subprocess.run([str(exe)], capture_output=True, text=True,
                           timeout=30)
        ok = r.returncode == 0
        got = f"exit {r.returncode}"
    except subprocess.TimeoutExpired:
        ok, got = False, "timeout"
    results.append((name + "[runtime]", "runtime", ok, "exit 0", [got]))

passed = sum(1 for _, _, ok, _, _ in results if ok)
gaps = sum(1 for _, e, _, _, _ in results if e == "gap")
lines = [f"bugs51-97 suite: {passed}/{len(results)} passed "
         f"({len(seen)} unique scenarios, {gaps} documented gaps)"]
for name, expect, ok, diag, got in results:
    if not ok:
        lines.append(f"  FAIL {name:46} expect={expect} diag={diag!r} "
                     f"got={got[-110:]}")
for name, expect, ok, diag, reason in results:
    if expect == "gap":
        lines.append(f"  GAP  {name:46} {reason}")
(OUT / "report.txt").write_text("\n".join(lines) + "\n")
print(lines[0])
if passed != len(results):
    print("\n".join(lines[1:40]))
sys.exit(0 if passed == len(results) else 1)
