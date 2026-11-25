#!/bin/sh
# Build and exercise the five developer utilities, then print the aggregate:
#
#   UTIL_DEV <tools> <passed> <failed> <refused>
#
# A tool is built with the exact command:
#   ./build/c+ build <f>.cp --strict --sanitize=none -I. -o <bin>
# Every invocation runs under `timeout 30`.  A "pass" requires both the exact
# deterministic stdout and exit code 0.  A "refused" case requires the exact
# REFUSED line and a nonzero exit code.  Any mismatch increments <failed>.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../../.." && pwd)
cd "$ROOT" || exit 2

D=apps/legacy_examples/utilities/dev_tools
TMP=$(mktemp -d "${TMPDIR:-/tmp}/util-dev.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

tools=0
passed=0
failed=0
refused=0

# ---- fixed fixtures -------------------------------------------------------
printf 'Hello, IGI!\n' > "$TMP/small.bin"
printf '\000\001\177\200\377A' > "$TMP/bin.bin"
awk 'BEGIN { for (i = 0; i < 257; i++) printf "A" }' > "$TMP/big.bin"
S65=""
i=0
while [ "$i" -lt 130 ]; do S65="${S65}1"; i=$((i + 1)); done
S130="$S65"
S64=$(printf '%s' "$S65" | cut -c 1-64)
S65=$(printf '%s' "$S65" | cut -c 1-65)

# ---- build with the exact required command --------------------------------
for name in hexdump numconv sha_lite envfilter timeconv; do
    tools=$((tools + 1))
    rm -f "$D/$name"
    if ! ./build/c+ build "$D/$name.cp" --strict --sanitize=none -I. \
            -o "$D/$name" > "$TMP/$name.build" 2>&1; then
        printf 'UTIL_BUILD %s FAIL build-command-error\n' "$name"
        cat "$TMP/$name.build"
        failed=$((failed + 1))
        continue
    fi
    if [ ! -x "$D/$name" ]; then
        printf 'UTIL_BUILD %s FAIL no-binary\n' "$name"
        cat "$TMP/$name.build"
        failed=$((failed + 1))
    else
        printf 'UTIL_BUILD %s OK\n' "$name"
    fi
done

# ---- case helpers ---------------------------------------------------------
# check_ok <label> <expected-stdout> -- <command> [args...]
check_ok() {
    label=$1
    want=$2
    shift 2
    [ "$1" = "--" ] && shift
    timeout 30 "$@" > "$TMP/out" 2>&1
    rc=$?
    got=$(cat "$TMP/out")
    if [ "$rc" -eq 0 ] && [ "$got" = "$want" ]; then
        printf 'UTIL_CASE %s PASS\n' "$label"
        passed=$((passed + 1))
    else
        printf 'UTIL_CASE %s FAIL rc=%d got=[%s] want=[%s]\n' \
            "$label" "$rc" "$got" "$want"
        failed=$((failed + 1))
    fi
}

# check_refused <label> <expected-stdout> -- <command> [args...]
check_refused() {
    label=$1
    want=$2
    shift 2
    [ "$1" = "--" ] && shift
    timeout 30 "$@" > "$TMP/out" 2>&1
    rc=$?
    got=$(cat "$TMP/out")
    if [ "$rc" -ne 0 ] && [ "$got" = "$want" ]; then
        printf 'UTIL_CASE %s REFUSED rc=%d\n' "$label" "$rc"
        refused=$((refused + 1))
    else
        printf 'UTIL_CASE %s FAIL expected-refusal rc=%d got=[%s] want=[%s]\n' \
            "$label" "$rc" "$got" "$want"
        failed=$((failed + 1))
    fi
}

# ---- hexdump --------------------------------------------------------------
EXP_HEX_SMALL='HEXDUMP 12 bytes
HEXDUMP 0000  48 65 6c 6c 6f 2c 20 49 47 49 21 0a              |Hello, IGI!.|'
EXP_HEX_BIN='HEXDUMP 6 bytes
HEXDUMP 0000  00 01 7f 80 ff 41                                |.....A|'
check_ok hexdump_small "$EXP_HEX_SMALL" -- "$D/hexdump" "$TMP/small.bin"
check_ok hexdump_binary "$EXP_HEX_BIN" -- "$D/hexdump" "$TMP/bin.bin"
check_refused hexdump_missing 'HEXDUMP REFUSED cannot-open' -- "$D/hexdump" "$TMP/nope"
check_refused hexdump_oversized 'HEXDUMP REFUSED oversized' -- "$D/hexdump" "$TMP/big.bin"

# ---- numconv --------------------------------------------------------------
check_ok numconv_dec_hex 'NUMCONV 255 dec hex ff' -- "$D/numconv" 255 dec hex
check_ok numconv_hex_bin 'NUMCONV ff hex bin 11111111' -- "$D/numconv" ff hex bin
check_ok numconv_bin_dec 'NUMCONV 101101 bin dec 45' -- "$D/numconv" 101101 bin dec
check_ok numconv_zero 'NUMCONV 0 dec bin 0' -- "$D/numconv" 0 dec bin
check_ok numconv_max 'NUMCONV 4294967295 dec hex ffffffff' -- "$D/numconv" 4294967295 dec hex
check_refused numconv_overflow 'NUMCONV REFUSED overflow' -- "$D/numconv" 4294967296 dec hex
check_refused numconv_bad_digit 'NUMCONV REFUSED invalid-digit' -- "$D/numconv" 102 bin dec
check_refused numconv_bad_base 'NUMCONV REFUSED unknown-base' -- "$D/numconv" 10 oct hex
check_refused numconv_empty 'NUMCONV REFUSED invalid-digit' -- "$D/numconv" '' dec hex
check_refused numconv_toolong 'NUMCONV REFUSED input-too-long' -- "$D/numconv" "$S65" dec hex

# ---- sha_lite -------------------------------------------------------------
check_ok sha_empty 'SHA_LITE 0 811c9dc5' -- "$D/sha_lite" ''
check_ok sha_hello 'SHA_LITE 5 4f9f2cab' -- "$D/sha_lite" hello
check_ok sha_abc 'SHA_LITE 3 1a47e90b' -- "$D/sha_lite" abc
check_ok sha_boundary64 "SHA_LITE 64 9c96df05" -- "$D/sha_lite" "$S64"
check_refused sha_toolong65 'SHA_LITE REFUSED input-too-long' -- "$D/sha_lite" "$S65"

# ---- envfilter ------------------------------------------------------------
EXP_ENV_MATCH='ENVFILTER match APP_NAME=igi
ENVFILTER match APP_MODE=test
ENVFILTER match APP_DEBUG=1
ENVFILTER APP_ 3'
check_ok envfilter_prefix "$EXP_ENV_MATCH" -- "$D/envfilter" APP_ \
    APP_NAME=igi APP_MODE=test OTHER=x APP_DEBUG=1
check_ok envfilter_none 'ENVFILTER Z_ 0' -- "$D/envfilter" Z_ A=1 B=2
check_refused envfilter_malformed 'ENVFILTER REFUSED malformed' -- "$D/envfilter" APP_ BADLINE
check_refused envfilter_empty_key 'ENVFILTER REFUSED empty-key' -- "$D/envfilter" APP_ =VALUE
check_refused envfilter_bad_key 'ENVFILTER REFUSED bad-key' -- "$D/envfilter" APP_ A-B=1
check_refused envfilter_oversized 'ENVFILTER REFUSED oversized' -- "$D/envfilter" APP_ "${S130}=1"

# ---- timeconv -------------------------------------------------------------
check_ok time_epoch 'TIMECONV 0 1970-01-01T00:00:00Z' -- "$D/timeconv" 0
check_ok time_leap 'TIMECONV 951782400 2000-02-29T00:00:00Z' -- "$D/timeconv" 951782400
check_ok time_2026 'TIMECONV 1767225600 2026-01-01T00:00:00Z' -- "$D/timeconv" 1767225600
check_ok time_max 'TIMECONV 253402300799 9999-12-31T23:59:59Z' -- "$D/timeconv" 253402300799
check_ok time_y2k38 'TIMECONV 2147483647 2038-01-19T03:14:07Z' -- "$D/timeconv" 2147483647
check_refused time_negative 'TIMECONV REFUSED out-of-range' -- "$D/timeconv" -1
check_refused time_over_max 'TIMECONV REFUSED out-of-range' -- "$D/timeconv" 253402300800
check_refused time_nonnum 'TIMECONV REFUSED invalid' -- "$D/timeconv" 12x
check_refused time_big 'TIMECONV REFUSED invalid' -- "$D/timeconv" 99999999999999999999

printf 'UTIL_DEV %d %d %d %d\n' "$tools" "$passed" "$failed" "$refused"
[ "$failed" -eq 0 ]
