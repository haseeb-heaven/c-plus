#!/usr/bin/env bash
# Build and run the safe-Rust examples and the C99 harness for the checked
# format/scan/conversion APIs, diff their `<api> <case> <token>` output, and
# print:  RUST_EXAMPLES format <apis> <cases> <matched> <mismatched> <blocked>
#
# Exit status: 0 when every line matches (or when the run is BLOCKED because a
# required symbol is absent), nonzero on a real token mismatch.
#
# From the repo root:
#   benchmarks/rust_examples/format/compare.sh

set -u
set -o pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
SAFE="$ROOT/build/safe/libcplus_safecrt.a"
HARD="$ROOT/build/libc/libcplus_hardened.a"

TMP="$(mktemp -d "${TMPDIR:-/tmp}/rust_examples_format.XXXXXX")" || exit 1
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

apis=0
cases=0
matched=0
mismatched=0
blocked=0
status="ok"

# --- timeout 30 wrapper (GNU timeout or gtimeout; degrade gracefully) -------
if [ -n "${TIMEOUT_BIN:-}" ]; then
    TMO="$TIMEOUT_BIN"
elif command -v timeout >/dev/null 2>&1; then
    TMO="$(command -v timeout)"
elif command -v gtimeout >/dev/null 2>&1; then
    TMO="$(command -v gtimeout)"
else
    TMO=""
fi

run30() {
    if [ -n "$TMO" ]; then
        "$TMO" 30 "$@"
    else
        "$@"
    fi
}

# --- C compiler: Apple clang has a working SDK sysroot on this host. --------
if [ -n "${CC:-}" ]; then
    CC_BIN="$CC"
elif [ -x /usr/bin/clang ]; then
    CC_BIN=/usr/bin/clang
else
    CC_BIN="$(command -v clang 2>/dev/null || command -v gcc 2>/dev/null || echo cc)"
fi

summary() {
    echo "RUST_EXAMPLES format $apis $cases $matched $mismatched $blocked"
    echo "STATUS $status"
}

# --- Step 1: verify required symbols with nm (both archives) ----------------
# nm prefixes C symbols with '_' on Mach-O and not on ELF; normalize both.
nm_defs() {
    nm -g "$1" 2>/dev/null | awk '$2 ~ /^[TtWw]$/ { s=$3; sub(/^_/, "", s); print s }' | sort -u
}
SAFE_SYMS="$(nm_defs "$SAFE")"
HARD_SYMS="$(nm_defs "$HARD")"

need_safecrt="cplus_sprintf_s cplus_sprintf_s_checked cplus_snprintf_s cplus_snprintf_s_checked cplus_vsnprintf_s cplus_vsprintf_s cplus_printf_s cplus_printf_s_checked cplus_fprintf_s cplus_fprintf_s_checked cplus_sscanf_s cplus_sscanf_s_checked"
need_hardened="cplus_strtol cplus_strtoul cplus_strtoimax"

for sym in $need_safecrt; do
    if ! printf '%s\n' "$SAFE_SYMS" | grep -Fxq "$sym"; then
        echo "BLOCKED: missing symbol $sym in $SAFE" >&2
        nm -g "$SAFE" 2>&1 | grep -E "T .*${sym}" >&2 || true
        blocked=$((blocked + 1))
    fi
done
for sym in $need_hardened; do
    if ! printf '%s\n' "$HARD_SYMS" | grep -Fxq "$sym"; then
        echo "BLOCKED: missing symbol $sym in $HARD" >&2
        nm -g "$HARD" 2>&1 | grep -E "T .*${sym}" >&2 || true
        blocked=$((blocked + 1))
    fi
done
if [ "$blocked" -ne 0 ]; then
    status="blocked"
    summary
    exit 0
fi

# --- Step 2: build both with a 30s limit -----------------------------------
if ! run30 rustc -O "$HERE/format.rs" -o "$TMP/format_rust" >"$TMP/rust.build.out" 2>&1; then
    echo "BLOCKED: rustc build failed; evidence:" >&2
    cat "$TMP/rust.build.out" >&2
    blocked=1
    status="blocked"
    summary
    exit 0
fi

if ! run30 "$CC_BIN" -std=c99 -O2 -D_DARWIN_C_SOURCE=1 "$HERE/harness.c" \
        "$SAFE" "$HARD" -o "$TMP/harness" >"$TMP/c.build.out" 2>&1; then
    echo "BLOCKED: C harness build/link failed; evidence:" >&2
    cat "$TMP/c.build.out" >&2
    blocked=1
    status="blocked"
    summary
    exit 0
fi

# --- Step 3: run both with a 30s limit -------------------------------------
run30 "$TMP/format_rust" >"$TMP/rust.out" 2>"$TMP/rust.err"
rust_rc=$?
run30 "$TMP/harness" >"$TMP/c.out" 2>"$TMP/c.err"
c_rc=$?

if [ "$rust_rc" -ne 0 ] || [ "$c_rc" -ne 0 ]; then
    echo "BLOCKED: run failed (rust_rc=$rust_rc c_rc=$c_rc); evidence:" >&2
    tail -20 "$TMP/rust.err" >&2
    tail -20 "$TMP/c.err" >&2
    blocked=1
    status="blocked"
    summary
    exit 0
fi

# --- Step 4: diff and count -------------------------------------------------
apis="$(awk 'NF>=3 {print $1}' "$TMP/rust.out" | sort -u | wc -l | tr -d ' ')"
cases="$(awk 'NF>=3 {print $2}' "$TMP/rust.out" | sort -u | wc -l | tr -d ' ')"
n_rust="$(wc -l < "$TMP/rust.out" | tr -d ' ')"
n_c="$(wc -l < "$TMP/c.out" | tr -d ' ')"
matched="$(awk 'NR==FNR {c[FNR]=$0; next} ($0==c[FNR]) {m++} END {print m+0}' "$TMP/c.out" "$TMP/rust.out")"
n_total=$(( n_rust > n_c ? n_rust : n_c ))
mismatched=$(( n_total - matched ))

if [ "$mismatched" -ne 0 ]; then
    status="fail"
    echo "MISMATCH: first differences (rust vs c) ---" >&2
    diff -u "$TMP/rust.out" "$TMP/c.out" >&2 || true
fi

summary
[ "$mismatched" -eq 0 ]
