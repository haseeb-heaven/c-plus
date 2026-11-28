#!/bin/sh
# Per-method memory checked-API vs safe-Rust token comparison driver.
# Builds 4 deterministic C harnesses + 1 Rust harness, runs each under
# `timeout 30`, diffs the "<method> <case> <token>" lines, prints
# "METHOD_MEMORY <cases> <matched> <mismatched>" and exits nonzero on any
# mismatch, missing key, harness build failure, or harness timeout/crash.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../../.." && pwd)

CLANG=${CLANG:-/usr/bin/clang}
RUSTC=${RUSTC:-rustc}
TIMEOUT=${TIMEOUT:-timeout}
CFLAGS="-std=c99 -I$ROOT/libc/install-include -I$ROOT/third_party/safeclib/include"
LIBS="$ROOT/build/safe/libcplus_safecrt.a $ROOT/build/libc/libcplus_hardened.a -lm -pthread"
METHODS="memcpy_s memmove_s memset_s memcmp_s"

command -v "$TIMEOUT" >/dev/null 2>&1 || { echo "missing timeout tool: $TIMEOUT" >&2; exit 2; }
[ -f "$ROOT/build/safe/libcplus_safecrt.a" ] || { echo "missing libcplus_safecrt.a" >&2; exit 2; }
[ -f "$ROOT/build/libc/libcplus_hardened.a" ] || { echo "missing libcplus_hardened.a" >&2; exit 2; }

mkdir -p "$HERE/bin" "$HERE/out"
CTOK="$HERE/out/c_tokens.txt"
RTOK="$HERE/out/rust_tokens.txt"
: > "$CTOK"
: > "$RTOK"

{
    echo "clang: $("$CLANG" --version 2>&1 | head -1)"
    echo "rustc: $("$RUSTC" --version 2>&1)"
    echo "date:  $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
} > "$HERE/out/toolchains.txt"

run_c() {
    m=$1
    "$CLANG" $CFLAGS "$HERE/$m.c" $LIBS -o "$HERE/bin/c_$m" || return 2
    "$TIMEOUT" 30 "$HERE/bin/c_$m" \
        > "$HERE/out/c_$m.stdout" 2> "$HERE/out/c_$m.stderr"
    ec=$?
    [ "$ec" -eq 0 ] || { echo "C harness $m exited $ec" >&2; return 2; }
    cat "$HERE/out/c_$m.stdout" >> "$CTOK"
}

for m in $METHODS; do
    run_c "$m" || exit 2
done

"$RUSTC" -O "$HERE/memory.rs" -o "$HERE/bin/rust_memory" || exit 2
"$TIMEOUT" 30 "$HERE/bin/rust_memory" \
    > "$RTOK.raw" 2> "$HERE/out/rust.stderr"
rec=$?
[ "$rec" -eq 0 ] || { echo "Rust harness exited $rec" >&2; exit 2; }
cat "$RTOK.raw" > "$RTOK"

sort "$CTOK" > "$HERE/out/c_sorted.txt"
sort "$RTOK" > "$HERE/out/rust_sorted.txt"
if ! diff -u "$HERE/out/c_sorted.txt" "$HERE/out/rust_sorted.txt" \
        > "$HERE/out/diff.txt" 2>&1; then
    echo "token diff (sorted) was non-empty; see out/diff.txt" >&2
fi

read cases matched mismatched <<EOF
$(awk '
    FNR == NR { c[$1 "|" $2] = $3; keys[$1 "|" $2] = 1; next }
    {
        k = $1 "|" $2
        keys[k] = 1
        if (!(k in c)) { mism++; print "MISSING_C " $0 > "/dev/stderr" }
        else if (c[k] == $3) { matchc++ }
        else { mism++; print "MISMATCH " $0 " (C=" c[k] ") " > "/dev/stderr" }
        seen[k] = 1
    }
    END {
        for (k in keys) cases++
        for (k in c) if (!(k in seen)) { mism++; print "MISSING_R " k > "/dev/stderr" }
        print cases+0, matchc+0, mism+0
    }
' "$CTOK" "$RTOK" 2> "$HERE/out/mismatch_detail.txt")
EOF

echo "METHOD_MEMORY $cases $matched $mismatched"
[ "$mismatched" -eq 0 ] || exit 1
exit 0
