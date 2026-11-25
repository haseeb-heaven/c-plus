#!/bin/sh
# Issue #28 — RAII shadowing reproducer harness (Agent 2 / runtime + libraries).
# Builds each reproducer with the project C+ driver, runs it under a strict
# bound, and diffs its exact cleanup log against the CORRECT expected log.
# POSIX sh only; no Python, no `|| true`. Prints RAII_SHADOW <cases> <pass> <fail>.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
D=tests/raii_shadowing
OUT=$D/out
mkdir -p "$OUT"
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
CASES="control_nonowning repro_shadow_basic repro_shadow_deep repro_shadow_branch repro_shadow_loop repro_shadow_return repro_shadow_break_deep repro_shadow_move repro_shadow_partial repro_shadow_struct"
pass=0; fail=0

for c in $CASES; do
    if ! timeout 180 "$CPLUS" build --no-werror "$D/$c.cp" --default --sanitize=none -I. \
            -o "$OUT/$c" >"$OUT/$c.build.log" 2>&1; then
        printf 'RAII_CASE FAIL %s build\n' "$c"
        fail=$((fail+1))
        continue
    fi
    timeout 30 "$OUT/$c" >"$OUT/$c.actual" 2>"$OUT/$c.stderr"
    rc=$?
    if [ "$rc" -ne 0 ]; then
        printf 'RAII_CASE FAIL %s run_rc=%d\n' "$c" "$rc"
        fail=$((fail+1))
        continue
    fi
    if diff -u "$D/expected/$c.txt" "$OUT/$c.actual" >"$OUT/$c.diff" 2>&1; then
        printf 'RAII_CASE PASS %s\n' "$c"
        pass=$((pass+1))
    else
        printf 'RAII_CASE FAIL %s cleanup-log mismatch\n' "$c"
        fail=$((fail+1))
    fi
done

# Cross-TU case: resource type owned locally, factory + destructor in a second TU.
if timeout 180 "$CPLUS" build --no-werror "$D/repro_shadow_crosstu.cp" --default --sanitize=none \
        -I. -I"$D" -c -o "$OUT/repro_shadow_crosstu.o" >"$OUT/crosstu_b.log" 2>&1 \
   && timeout 180 "$CPLUS" build --no-werror "$D/raii_shared.cp" --default --sanitize=none \
        -I. -I"$D" -c -o "$OUT/raii_shared.o" >"$OUT/crosstu_a.log" 2>&1 \
   && link_extra="" \
   && for a in build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a; do
          # These are the safe-libc archives. They are a separate build target and
          # are absent in a compiler-only checkout, so link them only when built:
          # the case must still prove cross-TU cleanup against the shipped runtime.
          if [ -f "$a" ]; then link_extra="$link_extra $a"; fi
      done \
   && /usr/bin/clang "$OUT/repro_shadow_crosstu.o" "$OUT/raii_shared.o" \
        build/libcplus_runtime.a $link_extra -lm -pthread \
        -o "$OUT/repro_shadow_crosstu" >>"$OUT/crosstu_b.log" 2>&1; then
    timeout 30 "$OUT/repro_shadow_crosstu" >"$OUT/repro_shadow_crosstu.actual" 2>&1
    if diff -u "$D/expected/repro_shadow_crosstu.txt" \
           "$OUT/repro_shadow_crosstu.actual" >"$OUT/repro_shadow_crosstu.diff" 2>&1; then
        printf 'RAII_CASE PASS repro_shadow_crosstu\n'; pass=$((pass+1))
    else
        printf 'RAII_CASE FAIL repro_shadow_crosstu cleanup-log mismatch\n'; fail=$((fail+1))
    fi
else
    printf 'RAII_CASE FAIL repro_shadow_crosstu build\n'; fail=$((fail+1))
fi

printf 'RAII_SHADOW %d %d %d\n' "$((pass+fail))" "$pass" "$fail"
[ "$fail" -eq 0 ]
