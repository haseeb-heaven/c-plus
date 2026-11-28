#!/bin/sh
# memory_ownership regression runner (strict/no-sanitize FIRST, then UBSan).
#
#   sh tests/stdlib/memory_ownership/run.sh
#
# Builds each .cp test with the repo-built ./build/c+, then runs the binary,
# each step wrapped in `timeout 30` with SIGKILL escalation.  A timeout is a
# FAIL, never a pass.  stdout/stderr are diffed byte-for-byte against the
# checked-in .expected / .stderr.expected fixtures.  Logs are written only
# under tests/stdlib/memory_ownership/results/.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd) || exit 1
cd "$ROOT" || exit 1

CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
DIR="$ROOT/tests/stdlib/memory_ownership"
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

TESTS="mo_alloc_edges mo_ownership_lifetime"
fail=0

# Deliberate order: strict_none first, then ubsan.
for mode in strict_none ubsan; do
    case "$mode" in
        strict_none) FLAGS="--strict --sanitize=none" ;;
        ubsan) FLAGS="--strict --sanitize=undefined" ;;
    esac

    for t in $TESTS; do
        exe="$OUT/${t}_${mode}"
        blog="$OUT/${t}_${mode}.build.log"
        sout="$OUT/${t}_${mode}.stdout"
        serr="$OUT/${t}_${mode}.stderr"
        diffout="$OUT/${t}_${mode}.diff"

        printf 'BUILD %s %s\n' "$t" "$mode"
        timeout -k 5 30 "$CPLUS" build $FLAGS "$DIR/$t.cp" -o "$exe" \
            >"$blog" 2>&1
        brc=$?
        if [ "$brc" -ne 0 ]; then
            echo "BUILD-FAIL $t $mode rc=$brc (see $blog)"
            fail=1
            continue
        fi

        printf 'RUN   %s %s\n' "$t" "$mode"
        timeout -k 5 30 "$exe" >"$sout" 2>"$serr"
        rrc=$?
        if [ "$rrc" -eq 124 ] || [ "$rrc" -eq 137 ]; then
            echo "TIMEOUT $t $mode rc=$rrc"
            fail=1
            continue
        fi
        if [ "$rrc" -ne 0 ]; then
            echo "RUN-FAIL $t $mode rc=$rrc"
            fail=1
        fi

        : >"$diffout"
        bad=0
        diff -u "$DIR/$t.expected" "$sout" >>"$diffout" 2>&1 || bad=1
        diff -u "$DIR/$t.stderr.expected" "$serr" >>"$diffout" 2>&1 || bad=1
        if [ "$bad" -ne 0 ]; then
            echo "OUT-FAIL $t $mode (see $diffout)"
            head -n 12 "$diffout" | sed 's/^/           /'
            fail=1
            continue
        fi
        echo "PASS $t $mode rc=$rrc"
    done
done

if [ "$fail" -ne 0 ]; then
    echo "MEMORY_OWNERSHIP SUITE FAIL"
    exit 1
fi
echo "MEMORY_OWNERSHIP SUITE PASS"
exit 0
