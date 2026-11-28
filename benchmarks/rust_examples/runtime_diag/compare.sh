#!/usr/bin/env bash
# Driver: C+ checked runtime-diagnostic harness vs safe-Rust state model.
#
#   ./compare.sh
#
# Builds both harnesses under `timeout 30`, runs the C harness once in-process
# and twice more in isolated subprocesses for the trap ON/OFF cases (recording
# exit codes so a forced _exit(70) never kills the driver), diffs the
# `<api> <case> <token>` lines, and prints:
#   RUST_EXAMPLES runtime_diag <apis> <cases> <matched> <mismatched> <blocked>
# A real token mismatch (or an unexpected trap exit code) makes exit status
# nonzero. BLOCKED symbols do not affect the exit status.

set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
cd "$ROOT" || exit 2

CC=/usr/bin/clang
RUSTC=rustc
ARCHIVE_SAFE=build/safe/libcplus_safecrt.a
ARCHIVE_LIBC=build/libc/libcplus_hardened.a
HARNESS_SRC=benchmarks/rust_examples/runtime_diag/harness.c
RUST_SRC=benchmarks/rust_examples/runtime_diag/runtime_diag.rs
TIMEOUT=${RUNTIME_DIAG_TIMEOUT:-30}

tmp="$(mktemp -d "${TMPDIR:-/tmp}/runtime_diag.XXXXXX")" || exit 2
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

# ---------------------------------------------------------------- symbols ---
for arc in "$ARCHIVE_SAFE" "$ARCHIVE_LIBC"; do
  if [ ! -f "$arc" ]; then
    echo "FATAL missing archive: $arc" >&2
    exit 2
  fi
done
if ! nm -g --defined-only "$ARCHIVE_SAFE" "$ARCHIVE_LIBC" >"$tmp/nm_all.txt" 2>"$tmp/nm.err"; then
  echo "FATAL nm failed" >&2
  cat "$tmp/nm.err" >&2
  exit 2
fi

# Every required symbol must be a defined text symbol before we call it.
verify_symbols() {
  missing=0
  for s in "$@"; do
    if ! grep -Eq " T _${s}$" "$tmp/nm_all.txt"; then
      echo "FATAL required symbol absent from archives: $s" >&2
      missing=1
    fi
  done
  [ "$missing" -eq 0 ]
}

verify_symbols \
  cplus_runtime_set_violation_trap cplus_runtime_get_violation_trap \
  cplus_runtime_violation \
  cplus_runtime_set_failstop cplus_runtime_get_failstop \
  cplus_runtime_push_cleanup cplus_runtime_cleanup_mark \
  cplus_runtime_cleanup_depth cplus_runtime_cleanup_unwind_to \
  cplus_runtime_unwound_cleanups cplus_runtime_guard \
  cplus_runtime_set_handler cplus_runtime_report \
  cplus_safe_ptr_is_live cplus_safe_ptr_size cplus_safe_ptr_elem_size \
  cplus_safe_ptr_count cplus_safe_query_length \
  cplus_safe_remaining_capacity cplus_safe_alloc_get_meta \
  cplus_safe_ptr_was_freed \
  cplus_safe_set_constraint_mode cplus_safe_get_constraint_mode \
  cplus_sc_set_mem_constraint_handler_s \
  cplus_sc_set_str_constraint_handler_s \
  cplus_except_init \
  cplus_atomic_init_int cplus_atomic_load_int cplus_atomic_store_int \
  cplus_atomic_exchange_int cplus_atomic_fetch_add_int \
  cplus_atomic_fetch_sub_int \
  cplus_atomic_compare_exchange_strong_int || exit 2

# Intentionally NOT compared: present in the archive, but unsafe/nondeterministic
# for a safe-Rust token comparison.  Evidence is printed with the nm result.
blocked=0
for s in cplus_atomic_is_lock_free_int cplus_atomic_compare_exchange_weak_int; do
  if grep -Eq " T _${s}$" "$tmp/nm_all.txt"; then
    case "$s" in
      cplus_atomic_is_lock_free_int)
        echo "BLOCKED $s nm=present(T) reason=host-specific result; safe Rust has no equivalent guaranteed-matching query"
        ;;
      cplus_atomic_compare_exchange_weak_int)
        echo "BLOCKED $s nm=present(T) reason=weak CAS may fail spuriously (C11 7.17.7.4); nondeterministic"
        ;;
    esac
  else
    echo "BLOCKED $s nm=absent reason=no defined symbol in either archive"
  fi
  blocked=$((blocked + 1))
done

# ----------------------------------------------------------------- build ---
if ! timeout "$TIMEOUT" "$CC" -std=c99 -O2 -pedantic \
      -Istdlib/safe -Ithird_party/exceptions4c \
      -Ithird_party/safeclib/include -Ilibc/install-include \
      "$HARNESS_SRC" "$ARCHIVE_SAFE" "$ARCHIVE_LIBC" \
      -lm -pthread -o "$tmp/harness_c" 2>"$tmp/c_build.err"; then
  echo "FATAL C build failed/timed out:" >&2
  cat "$tmp/c_build.err" >&2
  exit 2
fi

if ! timeout "$TIMEOUT" "$RUSTC" -O "$RUST_SRC" \
      -o "$tmp/harness_rs" 2>"$tmp/r_build.err"; then
  echo "FATAL Rust build failed/timed out:" >&2
  cat "$tmp/r_build.err" >&2
  exit 2
fi

# --------------------------------------------------------------- run C ------
if ! timeout "$TIMEOUT" "$tmp/harness_c" >"$tmp/c.out" 2>"$tmp/c.err"; then
  echo "FATAL C harness failed/timed out (rc=$?)" >&2
  cat "$tmp/c.err" >&2
  exit 2
fi

# Trap cases: fresh process each time; translate the exit code into a token.
timeout "$TIMEOUT" "$tmp/harness_c" --trap-on \
  >"$tmp/trap_on.out" 2>"$tmp/trap_on.err"
trap_on_rc=$?
if [ "$trap_on_rc" -eq 70 ]; then
  echo "runtime_violation_trap trap_on TRAP" >>"$tmp/c.out"
elif [ "$trap_on_rc" -eq 0 ] && grep -q '^runtime_violation_trap trap_on ' "$tmp/trap_on.out"; then
  grep '^runtime_violation_trap trap_on ' "$tmp/trap_on.out" >>"$tmp/c.out"
else
  echo "runtime_violation_trap trap_on BAD_RC_${trap_on_rc}" >>"$tmp/c.out"
  echo "NOTE trap_on unexpected rc=$trap_on_rc stderr=$(head -1 "$tmp/trap_on.err")" >&2
fi

timeout "$TIMEOUT" "$tmp/harness_c" --trap-off \
  >"$tmp/trap_off.out" 2>"$tmp/trap_off.err"
trap_off_rc=$?
if [ "$trap_off_rc" -eq 0 ] && grep -q '^runtime_violation_trap trap_off ' "$tmp/trap_off.out"; then
  grep '^runtime_violation_trap trap_off ' "$tmp/trap_off.out" >>"$tmp/c.out"
else
  echo "runtime_violation_trap trap_off BAD_RC_${trap_off_rc}" >>"$tmp/c.out"
  echo "NOTE trap_off unexpected rc=$trap_off_rc stderr=$(head -1 "$tmp/trap_off.err")" >&2
fi

# ------------------------------------------------------------- run Rust -----
if ! timeout "$TIMEOUT" "$tmp/harness_rs" >"$tmp/r.out" 2>"$tmp/r.err"; then
  echo "FATAL Rust harness failed/timed out (rc=$?)" >&2
  cat "$tmp/r.err" >&2
  exit 2
fi

# -------------------------------------------------------------- diff/count --
sort "$tmp/c.out" >"$tmp/c.sorted"
sort "$tmp/r.out" >"$tmp/r.sorted"
if ! diff -u "$tmp/r.sorted" "$tmp/c.sorted" >"$tmp/diff.txt" 2>&1; then
  sed 's/^/DIFF /' "$tmp/diff.txt"
fi

c_lines=$(awk 'END{print NR+0}' "$tmp/c.out")
r_lines=$(awk 'END{print NR+0}' "$tmp/r.out")
matched=$(comm -12 "$tmp/c.sorted" "$tmp/r.sorted" | awk 'END{print NR+0}')
c_only=$(comm -23 "$tmp/c.sorted" "$tmp/r.sorted" | awk 'END{print NR+0}')
r_only=$(comm -13 "$tmp/c.sorted" "$tmp/r.sorted" | awk 'END{print NR+0}')
mismatched=$((c_only + r_only))

# Any BAD_* / BAD_RC_* token in either stream is a real failure even if both
# sides happened to print the same bad word.
bad_lines=$(grep -Ec ' BAD_' "$tmp/c.out" "$tmp/r.out" | awk -F: '{s+=$2} END{print s+0}')
if [ "$bad_lines" -gt 0 ]; then
  mismatched=$((mismatched + bad_lines))
fi

apis=$(awk '{if(!seen[$1]++)n++} END{print n+0}' "$tmp/r.out")
cases=$r_lines

echo "RUST_EXAMPLES runtime_diag $apis $cases $matched $mismatched $blocked"
if [ "$mismatched" -ne 0 ]; then
  exit 1
fi
exit 0
