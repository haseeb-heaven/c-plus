#!/bin/sh
# Issue #55 arena regression runner.
#
#   sh tests/stdlib/arena/run.sh
#
# Builds tests/stdlib/arena/arena_reset_overflow.cp with the repo-built C+
# compiler in default/no-sanitize and default/UBSan modes, runs each binary
# under a bounded timeout, and diffs stdout byte-for-byte against the
# checked-in fixture.  Logs are written only under tests/stdlib/arena/results/.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd) || exit 1
cd "$ROOT" || exit 1

CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
DIR="$ROOT/tests/stdlib/arena"
OUT="$DIR/results"
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
export PATH

mkdir -p "$OUT" || exit 1

if ! command -v timeout >/dev/null 2>&1; then
    echo "error: 'timeout' utility not found; refusing unbounded tests" >&2
    exit 2
fi
if [ ! -x "$CPLUS" ]; then
    echo "error: C+ compiler missing: $CPLUS (build it first)" >&2
    exit 2
fi
fail=0
for mode in default_none ubsan; do
    case "$mode" in
        default_none) FLAGS="--default --no-werror --sanitize=none" ;;
        ubsan) FLAGS="--default --no-werror --sanitize=undefined" ;;
    esac
    exe="$OUT/arena_reset_overflow_$mode"
    blog="$OUT/arena_reset_overflow_$mode.build.log"
    sout="$OUT/arena_reset_overflow_$mode.stdout"
    serr="$OUT/arena_reset_overflow_$mode.stderr"
    diffout="$OUT/arena_reset_overflow_$mode.diff"

    printf 'BUILD %s\n' "$mode"
    timeout -k 5 60 "$CPLUS" build "$DIR/arena_reset_overflow.cp" $FLAGS -I"$ROOT" \
        -o "$exe" >"$blog" 2>&1
    brc=$?
    if [ "$brc" -ne 0 ]; then
        echo "BUILD-FAIL $mode rc=$brc (see $blog)"
        fail=1
        continue
    fi

    printf 'RUN   %s\n' "$mode"
    timeout -k 5 30 "$exe" >"$sout" 2>"$serr"
    rrc=$?
    if [ "$rrc" -eq 124 ] || [ "$rrc" -eq 137 ]; then
        echo "TIMEOUT $mode rc=$rrc"
        fail=1
        continue
    fi
    if [ "$rrc" -ne 0 ]; then
        echo "RUN-FAIL $mode rc=$rrc (see $sout)"
        fail=1
    fi

    : >"$diffout"
    bad=0
    diff -u "$DIR/arena_reset_overflow.expected" "$sout" >>"$diffout" 2>&1 || bad=1
    if [ "$bad" -ne 0 ]; then
        echo "OUT-FAIL $mode (see $diffout)"
        head -n 12 "$diffout" | sed 's/^/           /'
        fail=1
        continue
    fi
    echo "PASS $mode rc=$rrc"
done

if [ "$fail" -ne 0 ]; then
    echo "ARENA SUITE FAIL"
    exit 1
fi
echo "ARENA SUITE PASS"
exit 0
