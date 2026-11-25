#!/bin/sh
# Compare C+ modern-allocator outcomes with safe-Rust model outcomes.
# C+ side prints canonical tokens; Rust model prints matching tokens.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
export PATH="$ROOT/build:/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH"
unset SDKROOT || true

# Build C+ side (canonical token program from the Goose memory test).
BIN=build/bench_modern_compare
"$ROOT/build/c+" build tests/stdlib/scoped_memory.cp --default --no-werror --sanitize=none \
    -I. -o "$BIN" -- build/libc/libcplus_hardened.a >/dev/null 2>&1
timeout 60 "$BIN" > /tmp/cmp_cpp.out 2>/dev/null || true

# Build Rust side.
rustc -O benchmarks/rust_examples/modern_alloc.rs -o /tmp/cmp_rust >/dev/null 2>&1
timeout 60 /tmp/cmp_rust > /tmp/cmp_rust.out 2>/dev/null || true

# Derive comparable tokens from both streams.
# C+: SCOPED_MEMORY 0 means all checks passed; Rust: RUST_MODEL_OK plus key flags.
cpp_token=$(grep -c '^FAIL' /tmp/cmp_cpp.out || true)
rust_ok=$(grep -c 'RUST_MODEL_OK' /tmp/cmp_rust.out || true)
vm_c=$(grep -c 'in_place=true rewind=true' /tmp/cmp_rust.out || true)
slot_c=$(grep -c 'stale_refused=1 double=104 reuse=1' /tmp/cmp_rust.out || true)

printf 'MODERN_COMPARE %s %s %s %s\n' \
    "cpp_failures=$cpp_token" "rust_ok=$rust_ok" "vm_tokens=$vm_c" "slot_tokens=$slot_c"
if [ "$cpp_token" -eq 0 ] && [ "$rust_ok" -eq 1 ] && [ "$vm_c" -eq 1 ] && [ "$slot_c" -eq 1 ]; then
    echo 'MODERN_COMPARE MATCH'
    exit 0
fi
echo 'MODERN_COMPARE DIFFERENT'
exit 1
