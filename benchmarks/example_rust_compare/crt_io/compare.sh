#!/bin/sh
# Compare each checked-CRT C I/O example against its safe-Rust equivalent.
# Prints one line per example:
#   EXAMPLE_COMPARE <name> MATCH|DIFFERENT|BLOCKED <reason>
# and a final TOTALS line. All build artifacts stay inside this directory.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
BUILD="$HERE/.build"
mkdir -p "$BUILD"

CC=/usr/bin/clang
CFLAGS="-std=c99 -Ilibc/install-include -Ithird_party/safeclib/include -Istdlib/safe"
# "both archives": hardened libc plus the checked-CRT archive. The cplus_* I/O
# symbols resolve from libcplus_safecrt.a (verified: hardened alone leaves them
# undefined; safecrt alone links, but both are supplied per the task recipe).
ARCHIVES="$ROOT/build/libc/libcplus_hardened.a $ROOT/build/safe/libcplus_safecrt.a"
RUSTC=${RUSTC:-rustc}
RUSTFLAGS=${RUSTFLAGS:--O}
LIMIT=30
EXAMPLES="fopen_s fclose_s fread_s fwrite_s fprintf_s fscanf_s scanf_s"

# Run a command with a wall-clock limit. Uses /usr/bin/timeout when present,
# otherwise a python3 subprocess fallback. Exit 124 means "timed out".
run_limited() {
    secs=$1
    shift
    if command -v timeout >/dev/null 2>&1; then
        timeout "$secs" "$@"
    else
        python3 - "$secs" "$@" <<'PY'
import sys, subprocess
t = float(sys.argv[1]); cmd = sys.argv[2:]
try:
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=t)
except subprocess.TimeoutExpired:
    sys.exit(124)
sys.stdout.buffer.write(p.stdout)
sys.stderr.buffer.write(p.stderr)
sys.exit(p.returncode)
PY
    fi
}

match=0
different=0
blocked=0

for name in $EXAMPLES; do
    rs="$HERE/$name.rs"
    csrc="$ROOT/examples/crt/$name.c"

    if [ ! -f "$rs" ]; then
        echo "EXAMPLE_COMPARE $name BLOCKED missing Rust source: $rs"
        blocked=$((blocked + 1))
        continue
    fi
    if [ ! -f "$csrc" ]; then
        echo "EXAMPLE_COMPARE $name BLOCKED missing C source: $csrc"
        blocked=$((blocked + 1))
        continue
    fi

    # Static guarantee: the Rust side must contain no unsafe blocks or FFI.
    unsafe_hit=$(grep -nE 'unsafe[[:space:]]*\{|unsafe[[:space:]]+fn|extern[[:space:]]+"C"|#\[link' "$rs" || true)
    if [ -n "$unsafe_hit" ]; then
        echo "EXAMPLE_COMPARE $name BLOCKED unsafe/FFI detected in Rust: $unsafe_hit"
        blocked=$((blocked + 1))
        continue
    fi

    # Build the original C example with the exact task recipe (plus -Istdlib/safe,
    # which provides safe_io.h / safe_format.h / safe_error.h).
    if ! (cd "$ROOT" && $CC $CFLAGS "$csrc" $ARCHIVES -o "$BUILD/orig_$name") \
            >"$BUILD/orig_$name.build.log" 2>&1; then
        echo "EXAMPLE_COMPARE $name BLOCKED C build failed: $(head -n 1 "$BUILD/orig_$name.build.log")"
        blocked=$((blocked + 1))
        continue
    fi

    # Build the safe-Rust equivalent.
    if ! $RUSTC $RUSTFLAGS "$rs" -o "$BUILD/rust_$name" \
            >"$BUILD/rust_$name.build.log" 2>&1; then
        echo "EXAMPLE_COMPARE $name BLOCKED Rust build failed: $(head -n 1 "$BUILD/rust_$name.build.log")"
        blocked=$((blocked + 1))
        continue
    fi

    # Run both under the wall-clock limit; capture stdout, stderr and exit.
    run_limited "$LIMIT" "$BUILD/orig_$name" \
        >"$BUILD/orig_$name.stdout" 2>"$BUILD/orig_$name.stderr"
    orc=$?
    run_limited "$LIMIT" "$BUILD/rust_$name" \
        >"$BUILD/rust_$name.stdout" 2>"$BUILD/rust_$name.stderr"
    rrc=$?

    if [ "$orc" -eq 124 ] || [ "$rrc" -eq 124 ]; then
        echo "EXAMPLE_COMPARE $name BLOCKED timeout (>${LIMIT}s): orig=$orc rust=$rrc"
        blocked=$((blocked + 1))
        continue
    fi

    if [ "$orc" -ne "$rrc" ]; then
        echo "EXAMPLE_COMPARE $name DIFFERENT exit: orig=$orc rust=$rrc"
        different=$((different + 1))
        continue
    fi
    if ! cmp -s "$BUILD/orig_$name.stdout" "$BUILD/rust_$name.stdout"; then
        echo "EXAMPLE_COMPARE $name DIFFERENT stdout: orig=[$(cat "$BUILD/orig_$name.stdout")] rust=[$(cat "$BUILD/rust_$name.stdout")]"
        different=$((different + 1))
        continue
    fi

    echo "EXAMPLE_COMPARE $name MATCH stdout/exit (exit=$orc, stdout=[$(cat "$BUILD/orig_$name.stdout")])"
    match=$((match + 1))
done

echo "TOTALS match=$match different=$different blocked=$blocked"
[ "$different" -eq 0 ] && [ "$blocked" -eq 0 ]
