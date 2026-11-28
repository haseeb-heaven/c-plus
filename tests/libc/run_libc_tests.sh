#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
sh tests/libc/build_libc.sh >/dev/null
CC=${CPLUS_CLANG:-clang}
OUT=build/libc
LIB=$OUT/libcplus_hardened.a
# Linux portability (#32): glibc hides strnlen/TIME_UTC/memccpy under strict
# c99; atomic wrappers need libatomic; static archives must be link-ordered so
# a symbol referenced from the safecrt archive can still resolve from libc+.
PLATFORM_CFLAGS=""; PLATFORM_LIBS=""
case "$(uname -s)" in
  Linux) PLATFORM_CFLAGS="-D_GNU_SOURCE"; PLATFORM_LIBS="-latomic" ;;
esac
pass=0; fail=0
for t in tests/libc/test_*.c; do
  case "$t" in *_attack.c|*/test_corpus_format.c) continue ;; esac # adversarial suite has its own runner
  b="$OUT/$(basename "$t" .c)"
  EXTRA_INC=""; EXTRA_LIB=""
  case "$t" in
    *test_alloc_fault_matrix*) EXTRA_INC="-I."; EXTRA_LIB="build/safe/libcplus_safecrt.a" ;;
    *test_vendor_*) EXTRA_INC="-I. -Ithird_party/safeclib/include"; EXTRA_LIB="build/safe/libcplus_safecrt.a" ;;
  esac
  RUN_CMD="$b"
  if command -v timeout >/dev/null 2>&1; then
    # Large deterministic corpora can exceed 180s on a loaded CI runner
    # (observed: 760-case uninit corpus hit the 180s bound on ubuntu-latest).
    # 2026-11-24: PR CI run 45381886516 (linux-crt) hit the former 600s bound
    # on test_corpus_uninit.c after every prior test passed (exit 124).  The
    # local macOS run of the same binary finishes in 0.34s.  Keep this corpus
    # on the same 180s bound as its peers so a Linux hang is reported as a
    # bounded FAIL with diagnostics rather than consuming 10 minutes of CI.
    LIMIT=60
    case "$t" in *test_corpus_*) LIMIT=180 ;; esac
    RUN_CMD="timeout $LIMIT $b"
    if [ "$t" = "tests/libc/test_corpus_uninit.c" ]; then
      # Linux CI once hung in this corpus for the full bound. Trace cases and
      # arm an alarm so a hang reports its exact case instead of silence.
      RUN_CMD="env CPLUS_CORPUS_TRACE=1 CPLUS_CORPUS_WATCHDOG=120 timeout $LIMIT $b"
    fi
  fi
  # safecrt archive before libc+ so undefined refs resolve in one pass on GNU ld.
  # NOTE: `set -e` is active.  A failing command in the *body* of an if (not its
  # condition) aborts the whole runner before `brc=$?` / the FAIL block, so the
  # child's exit code leaks out as the runner's exit code and its diagnostics are
  # never printed.  That is why a Linux corpus_uninit watchdog exit 70 ended the
  # CI step with rc=70 and no case name.  Always capture status inside an if
  # condition, where errexit does not apply.
  if "$CC" -O2 -Wall -Wextra -pedantic -std=c99 $PLATFORM_CFLAGS \
        -Ilibc -Ilibc/include -Ilibc/install-include $EXTRA_INC "$t" \
        $EXTRA_LIB "$LIB" -lm -pthread $PLATFORM_LIBS -o "$b" \
        >/tmp/libc_build.out 2>&1; then
    brc=0
  else
    brc=$?
  fi
  if [ "$brc" -eq 0 ]; then
    if $RUN_CMD >/tmp/libc_test.out 2>&1; then
      rc=0
    else
      rc=$?
    fi
  else
    rc=99
  fi
  if [ "$rc" -eq 0 ]; then
    echo "ok $(basename "$t")"; pass=$((pass+1))
  else
    echo "FAIL $(basename "$t") (build=$brc run=$rc limit=${LIMIT:-n/a})"
    if [ "$t" = "tests/libc/test_corpus_uninit.c" ]; then
        # The test's signal handlers write the exact active case to stderr with
        # write(2) (immediate flush).  Echo those lines right after FAIL so a
        # CI log names the case even if the full trace is truncated, then show
        # the full trace: its last "corpus_uninit case N" line is the fallback
        # pin for a host that dies without running a handler.
        grep -E 'FINDING:|corpus_uninit case ' /tmp/libc_test.out 2>/dev/null | tail -4 || true
        cat /tmp/libc_test.out 2>/dev/null || true
    else
        tail -20 /tmp/libc_test.out 2>/dev/null || true
    fi
    tail -10 /tmp/libc_build.out 2>/dev/null || true
    fail=$((fail+1))
  fi
done
echo "libc tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
