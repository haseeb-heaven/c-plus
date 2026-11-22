#!/bin/sh
# End-to-end integration suite: C+ driver + hardened libc + checked SafeCRT.
#
#   sh tests/integration/run_integration.sh
#
# Deliberately `set -u` only: every command status is checked explicitly so a
# failure is counted instead of silently ending the script.  The PATH prefix
# avoids the broken default Homebrew clang sysroot on this machine; the system
# clang is used for both libraries and for the driver's lowering backend.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 1

export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
export CPLUS_CLANG=/usr/bin/clang

OUT="$ROOT/build/integration"
mkdir -p "$OUT"

PASS=0
FAIL=0

ok() {
    printf 'ok   %s\n' "$1"
    PASS=$((PASS + 1))
}

bad() {
    printf 'FAIL %s\n' "$1"
    if [ -n "${2:-}" ] && [ -f "$2" ]; then
        tail -n 20 "$2" 2>/dev/null || true
    fi
    FAIL=$((FAIL + 1))
}

# 1. Both libraries are built locally from source.
if sh scripts/build_safecrt.sh >"$OUT/build_safecrt.log" 2>&1; then
    ok "build/safe/libcplus_safecrt.a built"
else
    bad "build/safe/libcplus_safecrt.a built" "$OUT/build_safecrt.log"
fi
if sh tests/libc/build_libc.sh >"$OUT/build_libc.log" 2>&1; then
    ok "build/libc/libcplus_hardened.a built"
else
    bad "build/libc/libcplus_hardened.a built" "$OUT/build_libc.log"
fi

# 2. The real driver lowers, compiles and links app.cp against BOTH archives.
if ./build/c+ build --release tests/integration/app.cp \
        -I. -Istdlib/safe -Ilibc/install-include \
        -o "$OUT/app" -- \
        "$ROOT/build/safe/libcplus_safecrt.a" \
        "$ROOT/build/libc/libcplus_hardened.a" -lm -pthread \
        >"$OUT/app.build.log" 2>&1; then
    ok "c+ build --release app.cp (driver lowering + link)"
else
    bad "c+ build --release app.cp (driver lowering + link)" "$OUT/app.build.log"
fi

# 3. The produced program must exit 0 and announce success on stdout.
app_rc=0
if [ -x "$OUT/app" ]; then
    "$OUT/app" "$OUT/app_io.bin" >"$OUT/app.stdout" 2>"$OUT/app.stderr" || app_rc=$?
else
    app_rc=127
fi
if [ "$app_rc" -eq 0 ]; then
    ok "app exit status 0"
else
    bad "app exit status 0 (got $app_rc)" "$OUT/app.stderr"
fi
if grep -F -q 'integration: ok' "$OUT/app.stdout" 2>/dev/null; then
    ok "app stdout contains 'integration: ok'"
else
    bad "app stdout contains 'integration: ok'" "$OUT/app.stdout"
fi
# Logging is metadata-only: when TRACE is enabled the SafeCRT layer emits
# [SAFECRT] lines, and intentional refusals emit [CPLUS-SAFE] lines.  Neither
# may change the scenario results (app.cp compares the two snapshots itself).
if grep -F -q '[SAFECRT]' "$OUT/app.stderr" 2>/dev/null &&
   grep -F -q '[CPLUS-SAFE]' "$OUT/app.stderr" 2>/dev/null; then
    ok "logging enabled + refusal diagnostics visible"
else
    bad "logging enabled + refusal diagnostics visible" "$OUT/app.stderr"
fi

# 4. The compiler must REJECT the provable constant out-of-bounds index and
#    emit the substring recorded in expected_diag.txt (derived from the real
#    current diagnostic, not guessed).
probe_rc=0
./build/c+ check tests/integration/contract_probe.c \
    >"$OUT/probe.stdout" 2>"$OUT/probe.stderr" || probe_rc=$?
cat "$OUT/probe.stdout" "$OUT/probe.stderr" >"$OUT/probe.all" 2>/dev/null || true
if [ "$probe_rc" -ne 0 ]; then
    ok "c+ check rejects contract_probe.c (rc=$probe_rc)"
else
    bad "c+ check rejects contract_probe.c (rc=0)"
fi
if grep -F -q -f tests/integration/expected_diag.txt "$OUT/probe.all" 2>/dev/null; then
    ok "diagnostic contains expected substring"
else
    bad "diagnostic contains expected substring" "$OUT/probe.all"
fi

printf 'integration suite: %d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
