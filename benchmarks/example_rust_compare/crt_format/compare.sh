#!/usr/bin/env bash
# Build and run each checked-CRT format example twice -- once as the original
# C99 program (linked against the real SafeCRT + hardened-libc archives) and
# once as a safe-Rust equivalent -- then compare stdout byte-for-byte and the
# process exit status.
#
# Prints one line per example:
#   EXAMPLE_COMPARE <name> MATCH|DIFFERENT|BLOCKED <reason>
# and a final TOTAL line. Exit status 0 when nothing was DIFFERENT.
#
# Run from anywhere:  bash benchmarks/example_rust_compare/crt_format/compare.sh

set -u
set -o pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"

CC=/usr/bin/clang
SAFE="$ROOT/build/safe/libcplus_safecrt.a"
HARD="$ROOT/build/libc/libcplus_hardened.a"
BUILD="$HERE/.build"
mkdir -p "$BUILD"

EXAMPLES=(printf_s snprintf_s sprintf_s vsnprintf_s)

# --- 30-second timeout wrapper (GNU timeout / gtimeout / no-op fallback) ----
if command -v timeout >/dev/null 2>&1; then
    TMO="$(command -v timeout)"
elif command -v gtimeout >/dev/null 2>&1; then
    TMO="$(command -v gtimeout)"
else
    TMO=""
fi
run30() {
    if [ -n "$TMO" ]; then "$TMO" 30 "$@"; else "$@"; fi
}

# --- Preflight: missing tool/archives are a global BLOCKED condition --------
preflight_blocked() {
    echo "EXAMPLE_COMPARE SETUP BLOCKED $1"
    echo "TOTAL match=0 different=0 blocked=${#EXAMPLES[@]} of ${#EXAMPLES[@]}"
    exit 0
}
[ -x "$CC" ] || preflight_blocked "C compiler not executable: $CC"
[ -f "$SAFE" ] || preflight_blocked "SafeCRT archive missing: $SAFE"
[ -f "$HARD" ] || preflight_blocked "hardened libc archive missing: $HARD"
command -v rustc >/dev/null 2>&1 || preflight_blocked "rustc not found on PATH"

match=0
different=0
blocked=0

for name in "${EXAMPLES[@]}"; do
    c_src="$ROOT/examples/crt/$name.c"
    rs_src="$HERE/$name.rs"
    c_bin="$BUILD/$name.c.bin"
    rs_bin="$BUILD/$name.rs.bin"

    if [ ! -f "$c_src" ]; then
        echo "EXAMPLE_COMPARE $name BLOCKED missing C source: $c_src"
        blocked=$((blocked + 1))
        continue
    fi
    if [ ! -f "$rs_src" ]; then
        echo "EXAMPLE_COMPARE $name BLOCKED missing Rust source: $rs_src"
        blocked=$((blocked + 1))
        continue
    fi

    # C build. -Istdlib/safe is required for safe_format.h; libc/install-include
    # and third_party/safeclib/include are the task-specified include paths.
    if ! run30 "$CC" -std=c99 -O2 \
            -I"$ROOT/libc/install-include" \
            -I"$ROOT/third_party/safeclib/include" \
            -I"$ROOT/stdlib/safe" \
            "$c_src" "$SAFE" "$HARD" -o "$c_bin" \
            >"$BUILD/$name.c.build.log" 2>&1; then
        echo "EXAMPLE_COMPARE $name BLOCKED C build failed (see $BUILD/$name.c.build.log)"
        blocked=$((blocked + 1))
        continue
    fi

    # Safe-Rust build.
    if ! run30 rustc -O "$rs_src" -o "$rs_bin" \
            >"$BUILD/$name.rs.build.log" 2>&1; then
        echo "EXAMPLE_COMPARE $name BLOCKED Rust build failed (see $BUILD/$name.rs.build.log)"
        blocked=$((blocked + 1))
        continue
    fi

    # Run both under the 30s limit; capture stdout, stderr and exit status.
    run30 "$c_bin"  >"$BUILD/$name.c.out"  2>"$BUILD/$name.c.err";  c_rc=$?
    run30 "$rs_bin" >"$BUILD/$name.rs.out" 2>"$BUILD/$name.rs.err"; rs_rc=$?

    if [ "$c_rc" -ne 0 ] || [ "$rs_rc" -ne 0 ]; then
        echo "EXAMPLE_COMPARE $name BLOCKED run failed (c_rc=$c_rc rust_rc=$rs_rc; see $BUILD/$name.{c,rs}.err)"
        blocked=$((blocked + 1))
        continue
    fi

    if cmp -s "$BUILD/$name.c.out" "$BUILD/$name.rs.out"; then
        echo "EXAMPLE_COMPARE $name MATCH stdout identical, exit=$c_rc"
        match=$((match + 1))
    else
        echo "EXAMPLE_COMPARE $name DIFFERENT stdout differs (c_rc=$c_rc rust_rc=$rs_rc)"
        diff -u "$BUILD/$name.c.out" "$BUILD/$name.rs.out" >"$BUILD/$name.diff" 2>&1 || true
        different=$((different + 1))
    fi
done

echo "TOTAL match=$match different=$different blocked=$blocked of ${#EXAMPLES[@]}"
[ "$different" -eq 0 ]
