#!/bin/sh
# Portable CRT conformance/safety corpus (issue #19, Agent 2 share).
#
# Each case is plain C: it is compiled unchanged by the C+ compiler (safe mode)
# and by the system C compiler, and both builds must agree.  Cases cover defined
# behaviour; undefined hazards are documented, never executed.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
CC=${CPLUS_CLANG:-clang}
OUT="$ROOT/build/crt"
mkdir -p "$OUT"
cd "$ROOT" || exit 1

PASS=0
FAIL=0
FAILED=""

run_case() { # file
  name=$(basename "$1" .c)
  if ! "$CPLUS" build "tests/crt/$1" -o "$OUT/$name.cplus" >"$OUT/$name.cplus.log" 2>&1; then
    FAIL=$((FAIL + 1)); FAILED="$FAILED
  - $name: C+ build failed (see build/crt/$name.cplus.log)"; printf '  FAIL %s (build)\n' "$name"; return
  fi
  if ! "$CC" -O2 -pthread -o "$OUT/$name.cc" "tests/crt/$1" >"$OUT/$name.cc.log" 2>&1; then
    FAIL=$((FAIL + 1)); FAILED="$FAILED
  - $name: native build failed"; printf '  FAIL %s (native build)\n' "$name"; return
  fi
  "$OUT/$name.cplus" >"$OUT/$name.cplus.out" 2>"$OUT/$name.cplus.err"; cp_rc=$?
  "$OUT/$name.cc" >"$OUT/$name.cc.out" 2>"$OUT/$name.cc.err"; cc_rc=$?
  if [ "$cp_rc" -ne 0 ] || [ "$cc_rc" -ne 0 ]; then
    FAIL=$((FAIL + 1)); FAILED="$FAILED
  - $name: exit C+=$cp_rc native=$cc_rc"; printf '  FAIL %s (exit C+=%s native=%s)\n' "$name" "$cp_rc" "$cc_rc"; return
  fi
  if ! diff -q "$OUT/$name.cc.out" "$OUT/$name.cplus.out" >/dev/null; then
    FAIL=$((FAIL + 1)); FAILED="$FAILED
  - $name: outputs differ (see build/crt/$name.*.out)"; printf '  FAIL %s (output differs)\n' "$name"; return
  fi
  PASS=$((PASS + 1)); printf '  ok   %s\n' "$name"
}

printf 'CRT conformance (C+ safe mode vs native clang -O2)\n'
for src in tests/crt/*.c; do
  [ -e "$src" ] || continue
  base=$(basename "$src")
  case "$base" in
    *probe_native.c|*native_safety_probe.c|*bench_*.c) continue ;;
    test_safe_*.c) continue ;;  # needs safe crt headers/lib; run by run_safecrt_tests.sh
  esac
  run_case "$base"
done

printf '\nNative facility probe\n'
if "$CPLUS" build tests/crt/probe_native.c -o "$OUT/probe.cplus" >/dev/null 2>&1; then
  printf '  C+ safe mode:\n'
  "$OUT/probe.cplus" | sed 's/^/    /'
fi
"$CC" -O2 tests/crt/probe_native.c -o "$OUT/probe.cc" >/dev/null 2>&1 &&
  { printf '  native -O2:\n'; "$OUT/probe.cc" | sed 's/^/    /'; }

printf '\nCRT corpus: %d passed, %d failed\n' "$PASS" "$FAIL"
if [ "$FAIL" -ne 0 ]; then printf '%s\n' "$FAILED"; exit 1; fi
exit 0
