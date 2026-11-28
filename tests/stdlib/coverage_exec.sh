#!/bin/sh
# EXECUTION coverage for the library headers (not just API call sites).
#
# The C+ driver preprocesses a test into one .i file; compiling that with
# -fprofile-instr-generate -fcoverage-mapping and running it records which
# lines of the *lowered* headers actually executed.  llvm-cov export then
# reports per-file line coverage for the .hp sources (the #line directives in
# the lowered output map regions back to them).
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
CC=${CPLUS_CLANG:-clang}
COV=${LLVM_COV:-llvm-cov}
PROFDATA=${LLVM_PROFDATA:-llvm-profdata}
# Prefer the working CommandLineTools copies; Homebrew's are unusable on some
# hosts (missing @rpath/libLLVM), which is documented in docs/LIMITATIONS.md.
if command -v xcrun >/dev/null 2>&1; then
  found=$({ xcrun --find llvm-cov; } 2>/dev/null || true)
  [ -n "$found" ] && COV="$found"
  found=$({ xcrun --find llvm-profdata; } 2>/dev/null || true)
  [ -n "$found" ] && PROFDATA="$found"
fi
if ! "$COV" --version >/dev/null 2>&1 || ! "$PROFDATA" --version >/dev/null 2>&1; then
  for base in /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin \
              /opt/homebrew/Cellar/llvm/*/bin /usr/local/Cellar/llvm/*/bin; do
    if "$base/llvm-cov" --version >/dev/null 2>&1; then
      COV="$base/llvm-cov"
      PROFDATA="$base/llvm-profdata"
    fi
  done
fi
OUT="$ROOT/build/cov"
RUNTIME="$ROOT/build/libcplus_runtime.a"
mkdir -p "$OUT"
cd "$ROOT" || exit 1

if [ ! -x "$COV" ] && ! command -v "$COV" >/dev/null 2>&1; then
  echo "coverage: llvm-cov not available; install LLVM to run execution coverage" >&2
  exit 2
fi

fail=0
report_one() { # test
  name="$1"
  [ -f "$OUT/$name.i" ] || return 0
  rm -f "$OUT/$name.profraw" "$OUT/$name.profdata"
  input=/dev/null
  [ -f "tests/stdlib/$name.stdin" ] && input="tests/stdlib/$name.stdin"
  LLVM_PROFILE_FILE="$OUT/$name.profraw" "$OUT/$name" <"$input" \
    >/dev/null 2>&1 || true
  [ -f "$OUT/$name.profraw" ] || return 0
  "$PROFDATA" merge -sparse "$OUT/$name.profraw" -o "$OUT/$name.profdata" || return 0
  "$COV" export "$OUT/$name" -instr-profile="$OUT/$name.profdata" -summary-only \
    >"$OUT/$name.json" 2>/dev/null || return 0
  python3 - "$OUT/$name.json" "$name" <<'PY'
import json, sys
data = json.load(open(sys.argv[1]))
total = covered = 0
for f in data.get("data", [{}])[0].get("files", []):
    name = f.get("filename", "")
    if "stdlib/" not in name or not name.endswith(".hp"):
        continue
    lines = f.get("summary", {}).get("lines", {})
    total += lines.get("count", 0)
    covered += lines.get("covered", 0)
unit_total = unit_covered = 0
for f in data.get("data", [{}])[0].get("files", []):
    lines = f.get("summary", {}).get("lines", {})
    unit_total += lines.get("count", 0)
    unit_covered += lines.get("covered", 0)
if total:
    print("  %-22s %d/%d stdlib lines executed (%.1f%%)" %
          (sys.argv[2], covered, total, 100.0 * covered / total))
else:
    print("  %-22s lowered unit: %d/%d lines executed (%.1f%%); per-header "
          "attribution unavailable on this host" %
          (sys.argv[2], unit_covered, unit_total,
           100.0 * unit_covered / unit_total if unit_total else 0.0))
PY
}

build_one() { # test
  name="$1"
  "$CPLUS" build "tests/stdlib/$name.cp" -E -o "$OUT/$name.i" >"$OUT/$name.i.log" 2>&1 || return 1
  "$CC" -O0 -g -fprofile-instr-generate -fcoverage-mapping -c "$OUT/$name.i" \
    -o "$OUT/$name.o" >>"$OUT/$name.i.log" 2>&1 || return 1
  "$CC" -fprofile-instr-generate "$OUT/$name.o" "$RUNTIME" -o "$OUT/$name" \
    -lm -lpthread >>"$OUT/$name.i.log" 2>&1 || return 1
  return 0
}

printf 'execution coverage (lowered library headers, per test)\n'
for t in foundation io_edge_cases log_test collections_test; do
  if build_one "$t"; then
    report_one "$t"
  else
    echo "  $t: build failed (see build/cov/$t.i.log)" >&2
    fail=1
  fi
done
exit "$fail"
