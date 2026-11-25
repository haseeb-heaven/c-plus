#!/usr/bin/env bash
# compare.sh -- per-method memory-safety comparison: C+ checked I/O vs Rust.
#
# Builds and runs both deterministic harnesses, diffs their
# `<method> <case> <token>` lines and prints:
#   METHOD_IO <cases> <matched> <mismatched> <blocked>
# Exit status is nonzero only on a real token mismatch (or a build/run error).
#
# All artifacts are written under this directory's results/ subfolder.

set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"

CLANG=/usr/bin/clang
SAFE="$ROOT/build/safe/libcplus_safecrt.a"
HARDENED="$ROOT/build/libc/libcplus_hardened.a"
INC="-I$ROOT/libc/install-include -I$ROOT/third_party/safeclib/include"
RESULTS="$HERE/results"
C_BIN="$RESULTS/io_harness"
R_BIN="$RESULTS/io_rust"
C_OUT="$RESULTS/c.out"
R_OUT="$RESULTS/rust.out"
DIFF="$RESULTS/diff.txt"

mkdir -p "$RESULTS"

fail() {
    printf 'compare.sh: %s\n' "$1" >&2
    exit 2
}

[ -f "$SAFE" ] || fail "missing checked-CRT archive: $SAFE"
[ -f "$HARDENED" ] || fail "missing hardened-libc archive: $HARDENED"
[ -x "$CLANG" ] || fail "missing clang: $CLANG"
command -v rustc >/dev/null 2>&1 || fail "rustc not found on PATH"
command -v timeout >/dev/null 2>&1 || fail "timeout not found on PATH"

# ---- tmpfile_s availability (drives the BLOCKED count) ------------------
# nm evidence: no cplus_tmpfile_s / tmpfile_s symbol in either archive.
if nm "$SAFE" "$HARDENED" 2>/dev/null | grep -Eq '(^|_)tmpfile_s$'; then
    BLOCKED=0
else
    BLOCKED=1
fi

# ---- build --------------------------------------------------------------
timeout 30 "$CLANG" -std=c99 $INC \
    "$HERE/io_harness.c" "$SAFE" "$HARDENED" -lm -pthread \
    -o "$C_BIN" || fail "C harness build failed"
timeout 30 rustc -O "$HERE/io.rs" -o "$R_BIN" || fail "Rust harness build failed"

# ---- run (deterministic, bounded) ---------------------------------------
timeout 30 "$C_BIN" > "$C_OUT" 2>"$RESULTS/c.err" || fail "C harness run failed"
timeout 30 "$R_BIN" > "$R_OUT" 2>"$RESULTS/rust.err" || fail "Rust harness run failed"

CASES=$(wc -l < "$C_OUT" | tr -d ' ')
R_CASES=$(wc -l < "$R_OUT" | tr -d ' ')

# ---- token diff ---------------------------------------------------------
: > "$DIFF"
diff -u "$C_OUT" "$R_OUT" >> "$DIFF" 2>&1 || true

COUNTS=$(paste "$C_OUT" "$R_OUT" | awk -F '\t' '
    {
        if ($1 == $2) {
            matched++
        } else {
            mismatched++
            printf "MISMATCH c=[%s] rust=[%s]\n", $1, $2 > "/dev/stderr"
        }
    }
    END { printf "%d %d\n", matched, mismatched }
')
MATCHED=$(printf '%s\n' "$COUNTS" | awk '{print $1}')
MISMATCHED=$(printf '%s\n' "$COUNTS" | awk '{print $2}')

printf 'METHOD_IO %s %s %s %s\n' "$CASES" "$MATCHED" "$MISMATCHED" "$BLOCKED"

if [ "$R_CASES" -ne "$CASES" ]; then
    printf 'compare.sh: case-count mismatch (c=%s rust=%s)\n' \
        "$CASES" "$R_CASES" >&2
    exit 1
fi
if [ "$MISMATCHED" -ne 0 ]; then
    exit 1
fi
exit 0
