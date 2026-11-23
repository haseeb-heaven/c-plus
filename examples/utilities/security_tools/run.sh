#!/bin/sh
# Build and exercise all five security-tool demonstrations under `timeout 30`.
#
#   UTIL_SECURITY <tools> <passed> <failed> <refused>
#
# A tool passes only if it builds, produces an executable, and every argv case
# exits with the expected code AND prints its expected deterministic line.
# `refused` counts observed uppercase REFUSED protocol lines in tool output.
# An unexpected success, a missing canary guard, or a build failure counts as a
# tool failure.  Cases that trigger a checked API refusal are expected to exit 2.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$ROOT" || exit 2

DIR=examples/utilities/security_tools
TMP=$(mktemp -d "${TMPDIR:-/tmp}/util-security.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

tools=0
passed=0
failed=0
refused=0

LONG32='AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'
LONG48='AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'
LONG65='AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'

build_tool() {
    name=$1
    shift
    tools=$((tools + 1))
    rm -f "$DIR/$name"
    if ! "$@" >"$TMP/$name.build" 2>&1 || [ ! -x "$DIR/$name" ]; then
        printf 'UTIL_TOOL %s FAIL build\n' "$name"
        cat "$TMP/$name.build"
        failed=$((failed + 1))
        return 1
    fi
    return 0
}

# run_case <tool> <expected-rc> <expected-substring> [argv...]
run_case() {
    name=$1
    expected_rc=$2
    pattern=$3
    shift 3
    timeout 30 "$DIR/$name" "$@" >"$TMP/$name.raw" 2>&1
    rc=$?
    # The vendored safeclib ignore handler prints a diagnostic on refusals.
    grep -v '^IGNORE CONSTRAINT HANDLER:' "$TMP/$name.raw" >"$TMP/$name.out"
    cat "$TMP/$name.out"
    n=$(grep -c 'REFUSED' "$TMP/$name.out" || true)
    refused=$((refused + n))
    if [ "$rc" -eq "$expected_rc" ] && grep -Fq "$pattern" "$TMP/$name.out"; then
        printf 'UTIL_CASE %s PASS rc=%d pattern=%s\n' "$name" "$rc" "$pattern"
        return 0
    fi
    printf 'UTIL_CASE %s FAIL rc=%d want=%d missing=%s\n' \
        "$name" "$rc" "$expected_rc" "$pattern"
    return 1
}

# ---------------------------------------------------------------- checksum ---
if build_tool checksum \
    ./build/c+ build "$DIR/checksum.cp" --strict --sanitize=none -I. \
    -o "$DIR/checksum" -- \
    build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
then
    ok=1
    run_case checksum 0 'CHECKSUM valid OK' abc || ok=0
    run_case checksum 2 'CHECKSUM oversize REFUSED code=101' "$LONG32$LONG48" || ok=0
    run_case checksum 2 'CHECKSUM empty REFUSED code=108' '' || ok=0
    if [ "$ok" -eq 1 ]; then passed=$((passed + 1)); else failed=$((failed + 1)); fi
fi

# ---------------------------------------------------------------- pwdcheck ---
if build_tool pwdcheck \
    ./build/c+ build "$DIR/pwdcheck.cp" --strict --sanitize=none -I. \
    -o "$DIR/pwdcheck" -- \
    build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
then
    ok=1
    run_case pwdcheck 0 'PWCHECK strong OK' 'Aa1!aaaa' || ok=0
    run_case pwdcheck 2 'PWCHECK weak REFUSED code=108' 'aaaaaaaa' || ok=0
    run_case pwdcheck 2 'PWCHECK oversize REFUSED code=101' "$LONG48" || ok=0
    if [ "$ok" -eq 1 ]; then passed=$((passed + 1)); else failed=$((failed + 1)); fi
fi

# ---------------------------------------------------------------- safe_copy ---
if build_tool safe_copy \
    ./build/c+ build "$DIR/safe_copy.cp" --strict --sanitize=none -I. \
    -o "$DIR/safe_copy" -- \
    build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
then
    ok=1
    run_case safe_copy 0 'SAFECOPY exact OK rc=0' exact || ok=0
    run_case safe_copy 2 'SAFECOPY over REFUSED code=406' over || ok=0
    run_case safe_copy 2 'SAFECOPY nullsrc REFUSED code=400' nullsrc || ok=0
    run_case safe_copy 2 'SAFECOPY null REFUSED code=400' null || ok=0
    run_case safe_copy 0 'SAFECOPY raw_child OK child_exit=9' raw || ok=0
    if [ "$ok" -eq 1 ]; then passed=$((passed + 1)); else failed=$((failed + 1)); fi
fi

# ---------------------------------------------------------------- safe_token ---
if build_tool safe_token \
    ./build/c+ build "$DIR/safe_token.cp" --strict --sanitize=none -I. \
    -o "$DIR/safe_token" -- \
    build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
then
    ok=1
    run_case safe_token 0 'SAFETOKEN valid OK n=4' 'alpha,beta;gamma delta' || ok=0
    run_case safe_token 2 'SAFETOKEN oversize REFUSED code=101' "$LONG65" || ok=0
    run_case safe_token 2 'SAFETOKEN long_token REFUSED code=101' 'abcdefghijklmnopq,ok' || ok=0
    run_case safe_token 2 'SAFETOKEN many_tokens REFUSED code=101' 'one two three four five' || ok=0
    if [ "$ok" -eq 1 ]; then passed=$((passed + 1)); else failed=$((failed + 1)); fi
fi

# -------------------------------------------------------------- bounds_check ---
if build_tool bounds_check \
    ./build/c+ build "$DIR/bounds_check.cp" --strict --sanitize=none -I. \
    -o "$DIR/bounds_check"
then
    ok=1
    run_case bounds_check 0 'BOUNDS exact OK' exact || ok=0
    run_case bounds_check 2 'BOUNDS one_over REFUSED code=101 cat=5' one-over || ok=0
    run_case bounds_check 0 'BOUNDS interior OK' interior || ok=0
    run_case bounds_check 2 'BOUNDS stale REFUSED code=103 cat=7' stale || ok=0
    if [ "$ok" -eq 1 ]; then passed=$((passed + 1)); else failed=$((failed + 1)); fi
fi

printf 'UTIL_SECURITY %d %d %d %d\n' "$tools" "$passed" "$failed" "$refused"
[ "$failed" -eq 0 ]
