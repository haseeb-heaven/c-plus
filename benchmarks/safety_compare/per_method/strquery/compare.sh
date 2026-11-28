#!/usr/bin/env bash
# Driver: deterministic C+ checked-API harness vs safe-Rust harness.
#
#   ./compare.sh
#
# Runs both binaries under `timeout 30`, compares the `<method> <case> <token>`
# lines, prints a per-method DETAIL breakdown and one summary line:
#   METHOD_STRQUERY <cases> <matched> <mismatched> <blocked>
# A real token mismatch (or a canary/BAD_* token) makes the exit status nonzero.
# BLOCKED methods do not affect the exit status.

set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
cd "$ROOT" || exit 2

CC=/usr/bin/clang
RUSTC=rustc
ARCHIVE=build/safe/libcplus_safecrt.a
HARNESS_SRC=benchmarks/safety_compare/per_method/strquery/harness.c
RUST_SRC=benchmarks/safety_compare/per_method/strquery/strquery.rs
TIMEOUT=${STRQUERY_TIMEOUT:-30}

tmp="$(mktemp -d "${TMPDIR:-/tmp}/strquery.XXXXXX")" || exit 2
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

METHODS="strlen_s strnlen_s strchr_s strrchr_s strstr_s strcasestr_s strspn_s strcspn_s"
CASES="valid null zero_bound one_over_bound not_found canary"

# --- availability from nm (the archive, not the sources, is the truth) -------
if [ ! -f "$ARCHIVE" ]; then
  echo "FATAL missing archive: $ARCHIVE" >&2
  exit 2
fi
if ! nm -g --defined-only "$ARCHIVE" >"$tmp/nm_all.txt" 2>/dev/null; then
  echo "FATAL nm failed on $ARCHIVE" >&2
  exit 2
fi

method_available() {
  grep -Eq "cplus_(sc|v)_.*${1}|_${1}([^A-Za-z0-9_]|$)" "$tmp/nm_all.txt"
}

# Call sites that harness.c actually provides.
is_implemented() {
  case "$1" in
    strnlen_s|strchr_s|strrchr_s|strstr_s|strcasestr_s|strspn_s|strcspn_s) return 0 ;;
    *) return 1 ;;
  esac
}

# --- build ------------------------------------------------------------------
if ! "$CC" -std=c99 -O2 -pedantic \
      -Ilibc/install-include -Ithird_party/safeclib/include \
      "$HARNESS_SRC" "$ARCHIVE" build/libc/libcplus_hardened.a \
      -lm -pthread -o "$tmp/harness_c" 2>"$tmp/c_build.err"; then
  echo "FATAL C build failed:" >&2
  cat "$tmp/c_build.err" >&2
  exit 2
fi

if ! "$RUSTC" -O "$RUST_SRC" -o "$tmp/harness_rs" 2>"$tmp/r_build.err"; then
  echo "FATAL Rust build failed:" >&2
  cat "$tmp/r_build.err" >&2
  exit 2
fi

# --- run under timeout ------------------------------------------------------
if ! timeout "$TIMEOUT" "$tmp/harness_c" >"$tmp/c.out" 2>"$tmp/c.err"; then
  echo "FATAL C harness timed out or failed (rc=$?)" >&2
  cat "$tmp/c.err" >&2
  exit 2
fi
if ! timeout "$TIMEOUT" "$tmp/harness_rs" >"$tmp/r.out" 2>"$tmp/r.err"; then
  echo "FATAL Rust harness timed out or failed (rc=$?)" >&2
  cat "$tmp/r.err" >&2
  exit 2
fi

# --- compare ----------------------------------------------------------------
total_cases=0
matched=0
mismatched=0
blocked=0
expected_lines=0

for m in $METHODS; do
  if ! method_available "$m"; then
    echo "BLOCKED $m (no cplus_sc_*/cplus_v_* symbol in $ARCHIVE; see nm_evidence.txt)"
    echo "DETAIL $m 0 0 0 1"
    blocked=$((blocked + 1))
    continue
  fi
  if ! is_implemented "$m"; then
    echo "NOTE $m present in archive but harness.c has no call site; not compared"
    echo "DETAIL $m 0 0 0 1"
    blocked=$((blocked + 1))
    continue
  fi

  m_cases=0
  m_matched=0
  m_mismatched=0
  for c in $CASES; do
    ct=$(grep -F "$m $c " "$tmp/c.out" | head -1 | awk '{print $3}')
    rt=$(grep -F "$m $c " "$tmp/r.out" | head -1 | awk '{print $3}')
    total_cases=$((total_cases + 1))
    m_cases=$((m_cases + 1))
    if [ -n "$ct" ] && [ "$ct" = "$rt" ]; then
      matched=$((matched + 1))
      m_matched=$((m_matched + 1))
    else
      mismatched=$((mismatched + 1))
      m_mismatched=$((m_mismatched + 1))
      echo "MISMATCH $m $c c='${ct:-<missing>}' rust='${rt:-<missing>}'"
    fi
  done
  expected_lines=$((expected_lines + m_cases))
  echo "DETAIL $m $m_cases $m_matched $m_mismatched 0"
done

# Structural checks: unexpected line counts, canary corruption, BAD_* tokens.
c_lines=$(awk 'END{print NR+0}' "$tmp/c.out")
r_lines=$(awk 'END{print NR+0}' "$tmp/r.out")
if [ "$c_lines" -ne "$expected_lines" ]; then
  echo "MISMATCH c_line_count got=$c_lines expected=$expected_lines"
  mismatched=$((mismatched + 1))
fi
if [ "$r_lines" -ne "$expected_lines" ]; then
  echo "MISMATCH rust_line_count got=$r_lines expected=$expected_lines"
  mismatched=$((mismatched + 1))
fi
if grep -q ' CANARY_BAD$' "$tmp/c.out" || grep -q ' CANARY_BAD$' "$tmp/r.out"; then
  echo "MISMATCH canary_corruption detected"
  mismatched=$((mismatched + 1))
fi
if grep -Eq ' BAD_(ACCEPTED|RESULT)$' "$tmp/c.out" || grep -Eq ' BAD_(ACCEPTED|RESULT)$' "$tmp/r.out"; then
  echo "MISMATCH unexpected_checked_api_result detected"
  mismatched=$((mismatched + 1))
fi

echo "METHOD_STRQUERY $total_cases $matched $mismatched $blocked"
if [ "$mismatched" -ne 0 ]; then
  exit 1
fi
exit 0
