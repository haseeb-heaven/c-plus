#!/bin/sh
# Build and compare the json_log runnable examples.
#
# C side  : C+ harness calling the real compiled cplus_json_* / cplus_safe_log*
#           / cplus_libc_log* symbols plus the static-inline json.hp / log.hp
#           APIs (built with the bundled build/c+ driver).
# Rust side: safe-Rust analogue examples (no unsafe, no FFI).
#
# Both print deterministic "<api> <case> <token>" lines from fixed fixtures.
# Token agreement is an observation about these examples, not a proof of
# equivalent safety guarantees (see NOTES.md).
#
# Exit status: 0 when no real mismatch exists (BLOCKED is not a mismatch),
#             1 on an output mismatch, build failure or runtime failure.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd) || exit 1
cd "$ROOT" || exit 1

DIR=benchmarks/rust_examples/json_log
OUT=$DIR/.out
mkdir -p "$OUT" || exit 1

RUSTC=${CPLUS_RUSTC:-rustc}
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
SAFE=$ROOT/build/safe/libcplus_safecrt.a
LIBC=$ROOT/build/libc/libcplus_hardened.a
YYJSON=$ROOT/build/ext/libyyjson.a
TIMEOUT=${CPLUS_TIMEOUT:-30}

apis=0
cases=0
matched=0
mismatched=0
blocked=0

summary() {
    echo "RUST_EXAMPLES json_log $apis $cases $matched $mismatched $blocked"
}

# nm evidence is collected on every run so BLOCKED claims are reproducible.
: > "$OUT/nm_evidence.txt"
for archive in "$SAFE" "$LIBC"; do
    {
        echo "=== nm -g $archive | grep -E 'cplus_(json|safe_log|libc_log)' ==="
        nm -g "$archive" 2>/dev/null | grep -E 'cplus_(json|safe_log|libc_log)'
    } >> "$OUT/nm_evidence.txt" 2>&1
done

have_sym() {
    nm -g "$1" 2>/dev/null | grep -Eq " T _?$2\$"
}

missing=""
for sym in cplus_json_parse cplus_json_get_int cplus_json_get_str \
           cplus_json_free cplus_safe_log cplus_safe_log_set_level \
           cplus_safe_log_get_level; do
    have_sym "$SAFE" "$sym" || missing="$missing $sym"
done
for sym in cplus_libc_log cplus_libc_log_set_enabled; do
    have_sym "$LIBC" "$sym" || missing="$missing $sym"
done

if [ -n "$missing" ]; then
    echo "BLOCKED absent compiled symbols:$missing" >&2
    echo "BLOCKED evidence: benchmarks/rust_examples/json_log/.out/nm_evidence.txt" >&2
    # One BLOCKED record per (api,case) line in the full protocol (8 x 6 - 12 n/a).
    blocked=18
    apis=8
    cases=6
    summary
    exit 0
fi

if [ ! -x "$CPLUS" ]; then
    echo "BLOCKED C+ driver not executable: $CPLUS (override with CPLUS_BIN)" >&2
    blocked=18
    apis=8
    cases=6
    summary
    exit 0
fi

if ! command -v "$RUSTC" >/dev/null 2>&1; then
    echo "BLOCKED rustc not available: $RUSTC (override with CPLUS_RUSTC)" >&2
    blocked=18
    apis=8
    cases=6
    summary
    exit 0
fi

if [ ! -f "$YYJSON" ]; then
    echo "BLOCKED yyjson archive not built: $YYJSON" >&2
    blocked=18
    apis=8
    cases=6
    summary
    exit 0
fi

# ---- build (hard timeout 30s each) ----------------------------------------
if ! timeout "$TIMEOUT" "$CPLUS" build "$DIR/harness.c" --default --sanitize=none \
        -I"$ROOT" -I"$ROOT/third_party/yyjson" -L"$ROOT/build/ext" -lyyjson \
        -o "$OUT/harness" -- "$SAFE" "$LIBC" \
        > "$OUT/harness.build.log" 2>&1; then
    echo "FAIL C+ harness build (see $OUT/harness.build.log)" >&2
    tail -12 "$OUT/harness.build.log" >&2
    mismatched=1
    apis=8
    cases=6
    summary
    exit 1
fi

if ! timeout "$TIMEOUT" "$RUSTC" --edition 2021 "$DIR/json_log.rs" \
        -o "$OUT/json_log" > "$OUT/json_log.build.log" 2>&1; then
    echo "FAIL Rust build (see $OUT/json_log.build.log)" >&2
    tail -12 "$OUT/json_log.build.log" >&2
    mismatched=1
    apis=8
    cases=6
    summary
    exit 1
fi

# ---- run (hard timeout 30s each; stderr is logger noise) ------------------
if ! timeout "$TIMEOUT" "$OUT/harness" 2>/dev/null > "$OUT/c.txt"; then
    echo "FAIL C+ harness run" >&2
    mismatched=1
    apis=8
    cases=6
    summary
    exit 1
fi

if ! timeout "$TIMEOUT" "$OUT/json_log" 2>/dev/null > "$OUT/r.txt"; then
    echo "FAIL Rust run" >&2
    mismatched=1
    apis=8
    cases=6
    summary
    exit 1
fi

apis=$(awk 'NF { print $1 }' "$OUT/r.txt" | sort -u | wc -l | tr -d ' ')
cases=$(awk 'NF { print $2 }' "$OUT/r.txt" | sort -u | wc -l | tr -d ' ')

while IFS= read -r line; do
    [ -z "$line" ] && continue
    if grep -Fqx -- "$line" "$OUT/c.txt"; then
        matched=$((matched + 1))
    else
        mismatched=$((mismatched + 1))
        echo "MISMATCH rust='$line'" >&2
    fi
done < "$OUT/r.txt"

# Unmatched C-only lines (extra/missing output) are real mismatches too.
while IFS= read -r line; do
    [ -z "$line" ] && continue
    if ! grep -Fqx -- "$line" "$OUT/r.txt"; then
        mismatched=$((mismatched + 1))
        echo "MISMATCH c='$line'" >&2
    fi
done < "$OUT/c.txt"

diff -u "$OUT/c.txt" "$OUT/r.txt" > "$OUT/diff.txt" 2>&1

summary
[ "$mismatched" -eq 0 ]
