#!/bin/sh
# thread2 — lifecycle regression runner (Agent G, PHASES 5-7).
#
# 1. Builds/runs the 1000-case forked harness against the PRE-FIX stdlib headers
 #    (the original async.hp is kept verbatim in before/async_original.hp) —
 #    this MUST fail (the two fixed defects are asserted).
# 2. Builds/runs the same harness against the CURRENT (fixed) headers — MUST pass.
# 3. Re-runs the additive checked-sync suite and the 100-scenario thread method
 #    suite (no weakening) and checks the ABI fingerprint is unchanged.
# No network, no wall-clock assertions, no live-game/injected-DLL claims.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
HERE="$ROOT/tests/defense/thread2"
OUT="$HERE/out"
TMP=${CPLUS_THREAD2_TMP:-/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/thread2}
CPLUS_CLANG_BIN=${CPLUS_CLANG:-/usr/bin/clang}
UVINC=/opt/homebrew/include
UVLIB=/opt/homebrew/lib
mkdir -p "$OUT" "$TMP"

CFLAGS="-O1 -g -std=c99 -Wall -Wextra -pedantic -I$ROOT -I$ROOT/libc -I$ROOT/libc/install-include"
FAIL=0

# ---- (1) BEFORE: exact pre-fix headers -------------------------------------
BEFORE="$TMP/before_headers"
rm -rf "$BEFORE"
mkdir -p "$BEFORE/stdlib"
cp "$ROOT"/stdlib/*.hp "$BEFORE/stdlib/"
cp "$HERE/before/async_original.hp" "$BEFORE/stdlib/async.hp"
cp "$HERE/test_thread2.cp" "$BEFORE/test_thread2.cp"
echo "== BEFORE build (pre-fix async.hp fixture) =="
(cd "$BEFORE" && "$ROOT/build/c+" build test_thread2.cp --strict --sanitize=none \
    -I. -I"$UVINC" -L"$UVLIB" -luv -o thread2_before) \
    >"$OUT/before.build.log" 2>&1
BR=$?
if [ "$BR" -ne 0 ]; then
    echo "BEFORE BUILD FAILED rc=$BR"; tail -12 "$OUT/before.build.log"; FAIL=$((FAIL+1))
else
    timeout 60 "$BEFORE/thread2_before" >"$OUT/before.run.log" 2>&1
    RR=$?
    BEFORE_SUM=$(grep '^THREAD2 ' "$OUT/before.run.log" | tail -1)
    echo "before_rc=$RR  $BEFORE_SUM"
    # The pre-fix tree MUST expose the defects (non-zero failures, non-zero exit).
    case "$RR:$BEFORE_SUM" in
        0:*) echo "FAIL: pre-fix harness unexpectedly passed"; FAIL=$((FAIL+1)) ;;
        *:"THREAD2 1000 1000 0") echo "FAIL: pre-fix harness unexpectedly clean"; FAIL=$((FAIL+1)) ;;
        *) echo "OK: pre-fix harness exposed the defects" ;;
    esac
fi

# ---- (2) AFTER: current fixed headers --------------------------------------
echo "== AFTER build (current fixed headers) =="
"$ROOT/build/c+" build "$HERE/test_thread2.cp" --strict --sanitize=none \
    -I"$ROOT" -I"$UVINC" -L"$UVLIB" -luv -o "$OUT/thread2_after" \
    >"$OUT/after.build.log" 2>&1
BR=$?
if [ "$BR" -ne 0 ]; then
    echo "AFTER BUILD FAILED rc=$BR"; tail -12 "$OUT/after.build.log"; FAIL=$((FAIL+1))
else
    timeout 60 "$OUT/thread2_after" >"$OUT/after.run.log" 2>&1
    RR=$?
    AFTER_SUM=$(grep '^THREAD2 ' "$OUT/after.run.log" | tail -1)
    echo "after_rc=$RR  $AFTER_SUM"
    if [ "$RR" -ne 0 ] || [ "$AFTER_SUM" != "THREAD2 1000 1000 0" ]; then
        echo "FAIL: fixed harness did not pass"; FAIL=$((FAIL+1))
    else
        echo "OK: fixed harness passed all 1000 cases"
    fi
    # Determinism: a second bounded run must be byte-identical.
    timeout 60 "$OUT/thread2_after" >"$OUT/after.run2.log" 2>&1
    if cmp -s "$OUT/after.run.log" "$OUT/after.run2.log"; then
        echo "OK: fixed harness output deterministic"
    else
        echo "FAIL: fixed harness output nondeterministic"; FAIL=$((FAIL+1))
    fi
fi

# ---- (3) additive checked-sync suite (must not be weakened) ----------------
echo "== checked-sync suite =="
"$CPLUS_CLANG_BIN" $CFLAGS "$ROOT/tests/libc/test_sync_checked.c" \
    "$ROOT/build/libc/libcplus_hardened.a" -pthread -o "$OUT/test_sync_checked" \
    >"$OUT/sync.build.log" 2>&1
if [ $? -ne 0 ]; then
    echo "sync_checked BUILD FAILED"; tail -8 "$OUT/sync.build.log"; FAIL=$((FAIL+1))
else
    timeout 30 "$OUT/test_sync_checked" >"$OUT/sync.run.log" 2>&1
    SYNC=$(grep '^sync_checked: ' "$OUT/sync.run.log" | tail -1)
    echo "sync_rc=$?  $SYNC"
    if [ "$SYNC" != "sync_checked: 156 0" ]; then
        echo "FAIL: checked-sync suite regressed"; FAIL=$((FAIL+1))
    else
        echo "OK: checked-sync suite unchanged (156 0)"
    fi
fi

# ---- (4) 100-scenario thread method suite (must not be weakened) -----------
echo "== method suite (hundred_logging_threads) =="
"$CPLUS_CLANG_BIN" -O1 -g -std=c99 -Wall -Wextra -pedantic \
    -I"$ROOT" -I"$ROOT/libc" -I"$ROOT/libc/install-include" \
    "$ROOT/tests/methods/hundred_logging_threads.c" \
    "$ROOT/build/libc/libcplus_hardened.a" -lm -pthread -o "$OUT/hundred_logging_threads" \
    >"$OUT/method.build.log" 2>&1
if [ $? -ne 0 ]; then
    echo "method suite BUILD FAILED"; tail -8 "$OUT/method.build.log"; FAIL=$((FAIL+1))
else
    timeout 60 "$OUT/hundred_logging_threads" >"$OUT/method.run.log" 2>&1
    MRC=$?
    MSUM=$(grep 'scenarios ok' "$OUT/method.run.log" | tail -1)
    echo "method_rc=$MRC  $MSUM"
    if [ "$MRC" -ne 0 ] || [ "$MSUM" != "hundred_logging_threads: 100/100 scenarios ok" ]; then
        echo "FAIL: method suite regressed"; FAIL=$((FAIL+1))
    else
        echo "OK: method suite unchanged (100/100)"
    fi
fi

# ---- (5) ABI fingerprint (no new symbols / layout changes) -----------------
echo "== ABI fingerprint =="
timeout 120 sh "$ROOT/tools/abi_compare/run.sh" compare \
    "$ROOT/tests/abi/fingerprints/baseline.txt" >"$OUT/abi.run.log" 2>&1
ARC=$?
ABILINE=$(grep 'ABI fingerprint' "$OUT/abi.run.log" | tail -1)
echo "abi_rc=$ARC  $ABILINE"
if [ "$ARC" -ne 0 ] || ! printf '%s' "$ABILINE" | grep -q 'MATCHES'; then
    echo "FAIL: ABI fingerprint changed"; FAIL=$((FAIL+1))
else
    echo "OK: ABI fingerprint MATCHES baseline"
fi

echo "THREAD2 RUNNER FAILURES=$FAIL"
[ "$FAIL" -eq 0 ]
