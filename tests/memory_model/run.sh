#!/bin/sh
# Memory-model evidence gate: C+ scoped datastack (no per-object heap) versus
# the dynamic-heap allocation model used by C/Rust/Zig/Goose.
# POSIX sh only, bounded timeouts, no Python. Prints one summary line:
#   MEMORY_MODEL_TEST <cases> <pass> <fail>
# A fresh tree must already provide build-alpha/c+, build/c+ and the hardened
# libc archive, exactly as the other scoped-memory gates expect.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
GOOSE_PATH=$PATH
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG

OUT=build/memory_model
mkdir -p "$OUT"
cases=0; pass=0; fail=0

# 1. C+ scoped-memory facade. Require its own SCOPED_MEM_TEST summary to report
#    zero failures (the runner also exits nonzero when any case fails).
cases=$((cases + 1))
LOG=$OUT/scoped_mem.log
if CPLUS_BIN=build-alpha/c+ timeout 900 sh tests/stdlib/scoped_mem/run.sh >"$LOG" 2>&1 \
   && grep -Eq '^SCOPED_MEM_TEST [0-9]+ [0-9]+ 0$' "$LOG"; then
    pass=$((pass + 1))
    printf 'SCOPED_MEM_OK %s\n' "$(grep -m1 '^SCOPED_MEM_TEST ' "$LOG")"
else
    fail=$((fail + 1))
    printf 'SCOPED_MEM_FAIL\n'
    tail -12 "$LOG"
fi

# 2. Bump-vs-heap benchmark verifier. BENCH_SCOPED_OK is this gate's marker for
#    the sub-gate; require the verifier's zero-failure summary and BENCH_OK.
cases=$((cases + 1))
LOG=$OUT/scoped_bump.log
if timeout 900 sh benchmarks/scoped_bump/run.sh >"$LOG" 2>&1 \
   && grep -Eq '^BENCH_SCOPED [0-9]+ [0-9]+ 0$' "$LOG" \
   && grep -qF 'BENCH_SCOPED PASS BENCH_OK' "$LOG"; then
    pass=$((pass + 1))
    printf 'BENCH_SCOPED_OK %s\n' "$(grep -m1 '^BENCH_SCOPED [0-9]' "$LOG")"
else
    fail=$((fail + 1))
    printf 'BENCH_SCOPED_FAIL\n'
    tail -12 "$LOG"
fi

# 3. Goose arena proof. Rewrite only the scale literal with sed into /tmp (no
#    Python), then require the PHASE arena line and its decimal checksum.
cases=$((cases + 1))
LOG=$OUT/goose_arena.log
GOOSE_SRC=/tmp/memory_model_portable_scale1_$$.goose
sed 's/var scale: i64 = 100;/var scale: i64 = 1;/' \
    benchmarks/six_way/portable.goose >"$GOOSE_SRC"
arena_sum=
if grep -qF 'var scale: i64 = 1;' "$GOOSE_SRC" \
   && PATH=$GOOSE_PATH timeout 900 goose run "$GOOSE_SRC" >"$LOG" 2>&1; then
    arena_sum=$(awk '/^PHASE arena$/{getline; print; exit}' "$LOG")
fi
rm -f "$GOOSE_SRC"
if printf '%s' "$arena_sum" | grep -Eq '^[0-9]+$'; then
    pass=$((pass + 1))
    printf 'GOOSE_ARENA_OK PHASE arena %s\n' "$arena_sum"
else
    fail=$((fail + 1))
    printf 'GOOSE_ARENA_FAIL\n'
    tail -20 "$LOG"
fi

printf 'MEMORY_MODEL_TEST %d %d %d\n' "$cases" "$pass" "$fail"
[ "$fail" -eq 0 ]
