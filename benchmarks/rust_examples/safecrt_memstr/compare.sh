#!/bin/sh
# Compare the C+ SafeCRT checked memory/string APIs against safe-Rust examples.
#
# Builds harness.c (clang -std=c99) and examples.rs (rustc -O), runs both under
# `timeout 30`, diffs the "<api> <case> <token>" lines, prints
#   RUST_EXAMPLES safecrt_memstr <apis> <cases> <matched> <mismatched>
# and exits nonzero on any real mismatch, missing key, build failure, or
# timeout/crash.
#
# Every API symbol is verified with `nm -g build/safe/libcplus_safecrt.a`
# before it is exercised. An API whose symbol is absent is reported as BLOCKED
# on stderr and excluded from BOTH binaries' argv (never fabricated).
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)

CLANG=${CLANG:-/usr/bin/clang}
RUSTC=${RUSTC:-rustc}
NM=${NM:-nm}
TIMEOUT=${TIMEOUT:-timeout}
SAFECRT="$ROOT/build/safe/libcplus_safecrt.a"
HARDENED="$ROOT/build/libc/libcplus_hardened.a"
INCLUDE="-I$ROOT/libc/install-include -I$ROOT/third_party/safeclib/include"

command -v "$TIMEOUT" >/dev/null 2>&1 || { echo "missing timeout: $TIMEOUT" >&2; exit 2; }
command -v "$NM" >/dev/null 2>&1 || { echo "missing nm: $NM" >&2; exit 2; }
[ -f "$SAFECRT" ] || { echo "missing $SAFECRT" >&2; exit 2; }
[ -f "$HARDENED" ] || { echo "missing $HARDENED" >&2; exit 2; }

mkdir -p "$HERE/out"
OUT="$HERE/out"
: > "$OUT/symbols.txt"
: > "$OUT/mismatch_detail.txt"

# nm_has SYMBOL : true iff SYMBOL is a defined text symbol in the SafeCRT lib.
nm_has() {
    "$NM" -g "$SAFECRT" 2>/dev/null | \
        awk -v s="_$1" '$2 == "T" && $3 == s { found = 1 } END { exit !found }'
}

api_symbol() {
    case "$1" in
        memcpy_s)       echo cplus_sc_memcpy_s ;;
        memmove_s)      echo cplus_sc_memmove_s ;;
        memset_s)       echo cplus_sc_memset_s ;;
        memzero_s)      echo cplus_sc_memzero_s ;;
        memcmp_s)       echo cplus_sc_memcmp_s ;;
        strcpy_s)       echo cplus_sc_strcpy_s ;;
        strncpy_s)      echo cplus_sc_strncpy_s ;;
        strcat_s)       echo cplus_sc_strcat_s ;;
        strncat_s)      echo cplus_sc_strncat_s ;;
        strnlen_s)      echo cplus_sc_strnlen_s ;;
        strtok_s)       echo cplus_sc_strtok_s ;;
        strerror_s)     echo cplus_sc_strerror_s ;;
        strcmp_s)       echo cplus_sc_strcmp_s ;;
        memcpy_s_auto)  echo cplus_memcpy_s_auto ;;
        memmove_s_auto) echo cplus_memmove_s_auto ;;
        memset_s_auto)  echo cplus_memset_s_auto ;;
        memcmp_s_auto)  echo cplus_memcmp_s_auto ;;
        strcpy_s_auto)  echo cplus_strcpy_s_auto ;;
        strncpy_s_auto) echo cplus_strncpy_s_auto ;;
        strcat_s_auto)  echo cplus_strcat_s_auto ;;
        strncat_s_auto) echo cplus_strncat_s_auto ;;
        *)              echo "" ;;
    esac
}

ALL_APIS="memcpy_s memmove_s memset_s memzero_s memcmp_s \
strcpy_s strncpy_s strcat_s strncat_s strnlen_s strtok_s strerror_s strcmp_s \
memcpy_s_auto memmove_s_auto memset_s_auto memcmp_s_auto \
strcpy_s_auto strncpy_s_auto strcat_s_auto strncat_s_auto"

# Support symbols must exist, otherwise the harness cannot run at all.
for sup in cplus_sc_set_mem_constraint_handler_s_wrapper \
           cplus_sc_set_str_constraint_handler_s_wrapper \
           cplus_runtime_set_violation_trap; do
    if nm_has "$sup"; then
        echo "SUPPORT OK $sup" >> "$OUT/symbols.txt"
    else
        echo "SUPPORT MISSING $sup" >> "$OUT/symbols.txt"
        echo "missing support symbol: $sup" >&2
        exit 2
    fi
done

ACTIVE=""
BLOCKED=""
for api in $ALL_APIS; do
    sym=$(api_symbol "$api")
    if [ -n "$sym" ] && nm_has "$sym"; then
        echo "API OK $api -> $sym" >> "$OUT/symbols.txt"
        ACTIVE="$ACTIVE $api"
    else
        echo "API BLOCKED $api -> $sym (symbol absent)" >> "$OUT/symbols.txt"
        BLOCKED="$BLOCKED $api"
    fi
done

if [ -n "$BLOCKED" ]; then
    echo "BLOCKED (symbol absent in libcplus_safecrt.a, skipped):$BLOCKED" >&2
fi
napis=0
for api in $ACTIVE; do napis=$((napis + 1)); done
[ "$napis" -gt 0 ] || { echo "no active APIs to compare" >&2; exit 2; }

{
    echo "clang: $($CLANG --version 2>&1 | head -1)"
    echo "rustc: $($RUSTC --version 2>&1)"
    echo "nm:    $($NM --version 2>&1 | head -1)"
    echo "date:  $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
} > "$OUT/toolchains.txt"

# --- build (fail closed) ---
$CLANG -std=c99 -Wall -Wextra -Werror $INCLUDE "$HERE/harness.c" \
    "$SAFECRT" "$HARDENED" -lm -pthread -o "$OUT/harness_bin" \
    2> "$OUT/c_build.err" || { echo "C harness build failed" >&2; cat "$OUT/c_build.err" >&2; exit 2; }

$RUSTC -O "$HERE/examples.rs" -o "$OUT/examples_bin" \
    2> "$OUT/rust_build.err" || { echo "Rust build failed" >&2; cat "$OUT/rust_build.err" >&2; exit 2; }

# --- run under timeout, passing only nm-verified APIs ---
$TIMEOUT 30 "$OUT/harness_bin" $ACTIVE > "$OUT/c.out" 2> "$OUT/c.err"
cec=$?
[ "$cec" -eq 0 ] || { echo "C harness exited $cec" >&2; exit 2; }

$TIMEOUT 30 "$OUT/examples_bin" $ACTIVE > "$OUT/rust.out" 2> "$OUT/rust.err"
rec=$?
[ "$rec" -eq 0 ] || { echo "Rust harness exited $rec" >&2; exit 2; }

# --- compare tokens keyed by "<api>|<case>" ---
sort "$OUT/c.out" > "$OUT/c.sorted"
sort "$OUT/rust.out" > "$OUT/rust.sorted"
if ! diff -u "$OUT/c.sorted" "$OUT/rust.sorted" > "$OUT/diff.txt" 2>&1; then
    echo "sorted token diff was non-empty; see out/diff.txt" >&2
fi

read cases matched mismatched <<EOF
$(awk '
    FNR == NR { ct[$1 "|" $2] = $3; keys[$1 "|" $2] = 1; next }
    {
        k = $1 "|" $2
        keys[k] = 1
        if (!(k in ct)) { mism++; print "MISSING_C " $0 > "/dev/stderr" }
        else if (ct[k] == $3) { matchc++ }
        else { mism++; print "MISMATCH " $0 " (C=" ct[k] ") " > "/dev/stderr" }
        seen[k] = 1
    }
    END {
        for (k in keys) cases++
        for (k in ct) if (!(k in seen)) { mism++; print "MISSING_R " k > "/dev/stderr" }
        print cases+0, matchc+0, mism+0
    }
' "$OUT/c.out" "$OUT/rust.out" 2> "$OUT/mismatch_detail.txt")
EOF

echo "RUST_EXAMPLES safecrt_memstr $napis $cases $matched $mismatched 0"
if [ "$mismatched" -ne 0 ]; then
    cat "$OUT/mismatch_detail.txt" >&2
    exit 1
fi
exit 0
