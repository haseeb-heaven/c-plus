#!/bin/sh
# Build and run each checked-CRT string example from examples/crt/ and its
# safe-Rust equivalent, then print exactly one result line per example:
#
#   EXAMPLE_COMPARE <name> MATCH|DIFFERENT|BLOCKED <reason>
#
# Each original is compiled with /usr/bin/clang -std=c99 using the two checked
# archives (SafeCRT + hardened libc); each Rust equivalent is compiled with
# rustc -O. Every build and every run is wrapped in `timeout 30`. All artifacts
# stay under this script's own out/ directory; no source outside this directory
# is modified. `EXAMPLE_COMPARE_NORMALIZED` lines are supplementary: they repeat
# the verdict after deleting safeclib's `IGNORE CONSTRAINT HANDLER:` diagnostic
# from the C stdout, isolating the examples' own logic.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
OUT="$HERE/out"
CLANG=${CLANG:-/usr/bin/clang}
RUSTC=${RUSTC:-rustc}
TIMEOUT=${TIMEOUT:-timeout}
NAMES="strcpy_s strncpy_s strcat_s strncat_s strcmp_s strnlen_s"

SAFECRT="$ROOT/build/safe/libcplus_safecrt.a"
HARDENED="$ROOT/build/libc/libcplus_hardened.a"
CFLAGS="-std=c99 -O2 -I$ROOT/libc/install-include -I$ROOT/third_party/safeclib/include -I$ROOT/stdlib/safe"

mkdir -p "$OUT"

emit() {
    printf 'EXAMPLE_COMPARE %s %s %s\n' "$1" "$2" "$3"
}

emit_norm() {
    printf 'EXAMPLE_COMPARE_NORMALIZED %s %s %s\n' "$1" "$2" "$3"
}

command -v "$TIMEOUT" >/dev/null 2>&1 || { echo "missing timeout binary: $TIMEOUT" >&2; exit 2; }
command -v "$CLANG" >/dev/null 2>&1 || { echo "missing clang: $CLANG" >&2; exit 2; }
command -v "$RUSTC" >/dev/null 2>&1 || { echo "missing rustc: $RUSTC" >&2; exit 2; }

{
    echo "clang: $($CLANG --version 2>&1 | head -1)"
    echo "rustc: $($RUSTC --version 2>&1)"
    echo "timeout: $(command -v "$TIMEOUT") (30s per build/run)"
    echo "date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    echo "cflags: $CFLAGS"
    echo "archives: $SAFECRT $HARDENED"
} > "$OUT/toolchains.txt"

n_match=0
n_diff=0
n_blocked=0
n_norm_match=0
n_norm_diff=0

for name in $NAMES; do
    csrc="$ROOT/examples/crt/$name.c"
    rsrc="$HERE/$name.rs"

    if [ ! -f "$csrc" ]; then
        emit "$name" BLOCKED "original source missing: examples/crt/$name.c"
        emit_norm "$name" BLOCKED "original source missing"
        n_blocked=$((n_blocked + 1))
        continue
    fi
    if [ ! -f "$rsrc" ]; then
        emit "$name" BLOCKED "Rust source missing: $name.rs"
        emit_norm "$name" BLOCKED "Rust source missing"
        n_blocked=$((n_blocked + 1))
        continue
    fi
    if [ ! -f "$SAFECRT" ] || [ ! -f "$HARDENED" ]; then
        emit "$name" BLOCKED "checked archives missing: $SAFECRT $HARDENED"
        emit_norm "$name" BLOCKED "checked archives missing"
        n_blocked=$((n_blocked + 1))
        continue
    fi

    # (1) original C example: the exact prescribed include paths + both archives.
    "$TIMEOUT" 30 "$CLANG" $CFLAGS "$csrc" "$SAFECRT" "$HARDENED" -lm -pthread \
        -o "$OUT/c_$name" >"$OUT/c_$name.build.log" 2>&1
    crc_build=$?
    if [ "$crc_build" -ne 0 ]; then
        emit "$name" BLOCKED "original clang build failed (rc=$crc_build, see out/c_$name.build.log)"
        emit_norm "$name" BLOCKED "original clang build failed"
        n_blocked=$((n_blocked + 1))
        continue
    fi

    # (2) safe-Rust equivalent.
    "$TIMEOUT" 30 "$RUSTC" -O "$rsrc" -o "$OUT/rust_$name" \
        >"$OUT/rust_$name.build.log" 2>&1
    rrc_build=$?
    if [ "$rrc_build" -ne 0 ]; then
        emit "$name" BLOCKED "rustc -O build failed (rc=$rrc_build, see out/rust_$name.build.log)"
        emit_norm "$name" BLOCKED "rustc build failed"
        n_blocked=$((n_blocked + 1))
        continue
    fi

    # (3) run both under timeout 30; capture stdout, stderr and exit status.
    "$TIMEOUT" 30 "$OUT/c_$name" >"$OUT/c_$name.stdout" 2>"$OUT/c_$name.stderr"
    crc=$?
    "$TIMEOUT" 30 "$OUT/rust_$name" >"$OUT/rust_$name.stdout" 2>"$OUT/rust_$name.stderr"
    rrc=$?

    if [ "$crc" -eq 124 ] || [ "$rrc" -eq 124 ]; then
        emit "$name" BLOCKED "timeout 30 exceeded (c=$crc rust=$rrc)"
        emit_norm "$name" BLOCKED "timeout 30 exceeded"
        n_blocked=$((n_blocked + 1))
        continue
    fi
    if [ "$crc" -gt 128 ] || [ "$rrc" -gt 128 ]; then
        emit "$name" BLOCKED "signal/crash (c=$crc rust=$rrc)"
        emit_norm "$name" BLOCKED "signal/crash"
        n_blocked=$((n_blocked + 1))
        continue
    fi

    # (4) exact observable comparison: stdout bytes + stderr bytes + exit code.
    stdout_state=identical
    cmp -s "$OUT/c_$name.stdout" "$OUT/rust_$name.stdout" || stdout_state=differs
    stderr_state=identical
    cmp -s "$OUT/c_$name.stderr" "$OUT/rust_$name.stderr" || stderr_state=differs

    if [ "$stdout_state" = identical ] && [ "$stderr_state" = identical ] && [ "$crc" -eq "$rrc" ]; then
        emit "$name" MATCH "stdout+stderr+exit identical (exit $crc)"
        n_match=$((n_match + 1))
    else
        reason="stdout $stdout_state, stderr $stderr_state, exit $crc vs $rrc"
        if [ "$stdout_state" = differs ] && grep -q '^IGNORE CONSTRAINT HANDLER:' "$OUT/c_$name.stdout"; then
            reason="$reason (C-only safeclib violation-handler line on stdout)"
        fi
        emit "$name" DIFFERENT "$reason (see out/)"
        n_diff=$((n_diff + 1))
    fi

    # (5) supplementary: strip the C violation-handler diagnostic and compare
    #     the examples' own output only.
    sed '/^IGNORE CONSTRAINT HANDLER:/d' "$OUT/c_$name.stdout" > "$OUT/c_$name.stdout.normalized"
    cp "$OUT/rust_$name.stdout" "$OUT/rust_$name.stdout.normalized"
    if cmp -s "$OUT/c_$name.stdout.normalized" "$OUT/rust_$name.stdout.normalized" \
       && cmp -s "$OUT/c_$name.stderr" "$OUT/rust_$name.stderr" \
       && [ "$crc" -eq "$rrc" ]; then
        emit_norm "$name" MATCH "stdout (handler line removed)+stderr+exit identical (exit $crc)"
        n_norm_match=$((n_norm_match + 1))
    else
        emit_norm "$name" DIFFERENT "stdout/stderr/exit still differ after removing C handler line"
        n_norm_diff=$((n_norm_diff + 1))
    fi
done

printf 'EXAMPLE_COMPARE_TOTALS MATCH=%d DIFFERENT=%d BLOCKED=%d\n' \
    "$n_match" "$n_diff" "$n_blocked"
printf 'EXAMPLE_COMPARE_NORMALIZED_TOTALS MATCH=%d DIFFERENT=%d\n' \
    "$n_norm_match" "$n_norm_diff"
exit 0
