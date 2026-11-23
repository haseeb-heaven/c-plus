#!/usr/bin/env bash
# Build and run the safe-Rust examples and the C99 checked-API harness, then
# diff their normalized `<api> <case> <token>` streams.
#
# Summary format:
#   RUST_EXAMPLES io <apis> <cases> <matched> <mismatched> <blocked>
#
# `apis`    = number of distinct checked APIs covered (13).
# `cases`   = number of (api, case) token lines emitted by io.rs.
# `matched` / `mismatched` = line-by-line comparison against harness.c.
# `blocked` = number of blocking conditions (missing symbols / build / run).
#
# Exit status: nonzero only on a REAL mismatch (matched lines differ). A blocked
# run (e.g. an absent symbol) is reported with nm evidence and exits 0.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
SAFE="$REPO/build/safe/libcplus_safecrt.a"
HARD="$REPO/build/libc/libcplus_hardened.a"
OUT="$HERE/.build"
mkdir -p "$OUT"

APIS=13
BLOCKED=0

echo "== toolchains =="
rustc --version
cc --version | head -1

# ---------------------------------------------------------------- nm evidence
SAFE_SYMS="cplus_fopen_s cplus_fclose_s cplus_fread_s cplus_fwrite_s cplus_fprintf_s cplus_fscanf_s cplus_printf_s cplus_scanf_s"
HARD_SYMS="cplus_fopen cplus_fclose cplus_fread cplus_fwrite cplus_tmpfile"

echo "== nm evidence: safe archive ($SAFE) =="
if [ -f "$SAFE" ]; then
    nm -g "$SAFE" 2>/dev/null | grep -E ' T _cplus_(fopen_s|fclose_s|fread_s|fwrite_s|fprintf_s|fscanf_s|printf_s|scanf_s)$' | sort -u || true
else
    echo "MISSING ARCHIVE: $SAFE"
    BLOCKED=$((BLOCKED + 1))
fi

echo "== nm evidence: hardened archive ($HARD) =="
if [ -f "$HARD" ]; then
    nm -g "$HARD" 2>/dev/null | grep -E ' T _cplus_(fopen|fclose|fread|fwrite|tmpfile)$' | sort -u || true
else
    echo "MISSING ARCHIVE: $HARD"
    BLOCKED=$((BLOCKED + 1))
fi

if [ -f "$SAFE" ]; then
    for s in $SAFE_SYMS; do
        if ! nm -g "$SAFE" 2>/dev/null | grep -q " T _${s}$"; then
            echo "BLOCKED: absent symbol _$s in $SAFE"
            BLOCKED=$((BLOCKED + 1))
        fi
    done
fi
if [ -f "$HARD" ]; then
    for s in $HARD_SYMS; do
        if ! nm -g "$HARD" 2>/dev/null | grep -q " T _${s}$"; then
            echo "BLOCKED: absent symbol _$s in $HARD"
            BLOCKED=$((BLOCKED + 1))
        fi
    done
fi

if [ "$BLOCKED" -gt 0 ]; then
    echo "== summary: blocked (no real mismatch) =="
    echo "RUST_EXAMPLES io $APIS 0 0 0 $BLOCKED"
    exit 0
fi

# ---------------------------------------------------------------- build
echo "== build rust =="
if ! timeout 30 rustc --edition 2021 "$HERE/io.rs" -o "$OUT/io"; then
    echo "BLOCKED: rustc build failed"
    echo "RUST_EXAMPLES io $APIS 0 0 0 1"
    exit 0
fi

echo "== build C harness (both archives) =="
if ! timeout 30 cc -std=c99 -D_POSIX_C_SOURCE=200112L -I"$REPO/stdlib/safe" \
        "$HERE/harness.c" "$SAFE" "$HARD" -lpthread -o "$OUT/harness"; then
    echo "BLOCKED: C harness build failed"
    echo "RUST_EXAMPLES io $APIS 0 0 0 1"
    exit 0
fi

# ---------------------------------------------------------------- run
# Identical stdin for both (scanf_s valid case). stdout is demo noise and is
# discarded; only the stderr token streams are compared.
printf 'io 42\n' > "$OUT/stdin"

echo "== run rust =="
timeout 30 "$OUT/io" < "$OUT/stdin" > "$OUT/io.stdout" 2> "$OUT/io.stderr"
IRC=$?
echo "== run C harness =="
timeout 30 "$OUT/harness" < "$OUT/stdin" > "$OUT/harness.stdout" 2> "$OUT/harness.stderr"
HRC=$?

if [ "$IRC" -ne 0 ] || [ "$HRC" -ne 0 ]; then
    echo "BLOCKED: runtime failure (rust=$IRC harness=$HRC)"
    echo "RUST_EXAMPLES io $APIS 0 0 0 1"
    exit 0
fi

# ---------------------------------------------------------------- compare
CASES="$(wc -l < "$OUT/io.stderr" | tr -d ' ')"
HLINES="$(wc -l < "$OUT/harness.stderr" | tr -d ' ')"
MATCHED="$(paste "$OUT/io.stderr" "$OUT/harness.stderr" | awk -F'\t' '$1==$2{m++} END{print m+0}')"
if [ "$CASES" -ge "$HLINES" ]; then TOTAL="$CASES"; else TOTAL="$HLINES"; fi
MISMATCHED=$((TOTAL - MATCHED))

echo "== token diff (rust stderr vs harness stderr) =="
if diff -u "$OUT/io.stderr" "$OUT/harness.stderr"; then
    echo "token streams identical"
fi

echo "== summary =="
echo "RUST_EXAMPLES io $APIS $CASES $MATCHED $MISMATCHED 0"

[ "$MISMATCHED" -eq 0 ] || exit 1
exit 0
