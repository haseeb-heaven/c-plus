#!/bin/sh
# scoped-memory-style memory facade proof. Prints: SCOPED_MEM_TEST <cases> <pass> <fail>.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
D=tests/stdlib/scoped_mem
pass=0; fail=0; cases=0
if ! timeout 300 ${CPLUS_BIN:-./build/c+} build "$D/test.cp" --default --no-werror --sanitize=none \
      -I. -o "$D/test" -- build/libc/libcplus_hardened.a >"$D/build.log" 2>&1; then
    echo "SCOPED_MEM_TEST FAIL build"; tail -6 "$D/build.log"; exit 1
fi
timeout 30 "$D/test" >"$D/out" 2>"$D/err" || { echo "SCOPED_MEM_TEST FAIL run"; cat "$D/out"; exit 1; }
cases=$((cases + 1))
if grep -qF 'SCOPED_MEM 0' "$D/out" && [ ! -s "$D/err" ]; then pass=$((pass + 1));
else fail=$((fail + 1)); echo "SCOPED_MEM_TEST FAIL output"; cat "$D/out"; fi

# Debug build: the popped region must read back as exact 0xDD.
cases=$((cases + 1))
if timeout 300 ${CPLUS_BIN:-./build/c+} build "$D/test_poison.cp" --default --no-werror --sanitize=none \
      -I. -o "$D/test_poison" -- build/libc/libcplus_hardened.a >"$D/build_poison.log" 2>&1 \
      && timeout 30 "$D/test_poison" >"$D/poison.out" 2>"$D/poison.err" \
      && grep -qF 'POISON_TEST 0' "$D/poison.out" && [ ! -s "$D/poison.err" ]; then
    pass=$((pass + 1));
else fail=$((fail + 1)); echo "SCOPED_MEM_TEST FAIL debug poison"; tail -6 "$D/build_poison.log"; cat "$D/poison.out" 2>/dev/null; fi

# Release build: no poison stores; one-store rewind leaves popped bytes intact.
cases=$((cases + 1))
if timeout 300 ${CPLUS_BIN:-./build/c+} build "$D/test_poison.cp" --default --no-werror --sanitize=none \
      -DNDEBUG -I. -o "$D/test_release" -- build/libc/libcplus_hardened.a >"$D/build_release.log" 2>&1 \
      && timeout 30 "$D/test_release" >"$D/release.out" 2>"$D/release.err" \
      && grep -qF 'POISON_TEST 0' "$D/release.out" && [ ! -s "$D/release.err" ]; then
    pass=$((pass + 1));
else fail=$((fail + 1)); echo "SCOPED_MEM_TEST FAIL release one-store"; tail -6 "$D/build_release.log"; cat "$D/release.out" 2>/dev/null; fi

# Chained spill blocks: overflow allocates a new chunk, stale refs are refused.
cases=$((cases + 1))
if timeout 300 ${CPLUS_BIN:-./build/c+} build "$D/test_chain.cp" --default --no-werror --sanitize=none \
      -I. -o "$D/test_chain" -- build/libc/libcplus_hardened.a >"$D/build_chain.log" 2>&1 \
      && timeout 30 "$D/test_chain" >"$D/chain.out" 2>"$D/chain.err" \
      && grep -qF 'CHAIN_TEST 0' "$D/chain.out" && [ ! -s "$D/chain.err" ]; then
    pass=$((pass + 1));
else fail=$((fail + 1)); echo "SCOPED_MEM_TEST FAIL chained blocks"; tail -8 "$D/build_chain.log"; cat "$D/chain.out" 2>/dev/null; fi

# Cross-TU generation uniqueness (C4): weak shared counter, not TU-local.
cases=$((cases + 1))
if timeout 300 ${CPLUS_BIN:-./build/c+} build "$D/gen_main.cp" "$D/gen_a.cp" "$D/gen_b.cp" \
      --default --no-werror --sanitize=none -I. -o "$D/gen_test" \
      -- build/libc/libcplus_hardened.a >"$D/build_gen.log" 2>&1 \
      && timeout 30 "$D/gen_test" >"$D/gen.out" 2>"$D/gen.err" \
      && grep -qF 'GEN_TEST 0' "$D/gen.out" && [ ! -s "$D/gen.err" ]; then
    pass=$((pass + 1));
else fail=$((fail + 1)); echo "SCOPED_MEM_TEST FAIL cross-TU generations"; tail -8 "$D/build_gen.log"; cat "$D/gen.out" 2>/dev/null; fi

# 16-byte alignment guarantee for every bump allocation.
cases=$((cases + 1))
if timeout 300 ${CPLUS_BIN:-./build/c+} build "$D/test_align.cp" --default --no-werror --sanitize=none \
      -I. -o "$D/test_align" -- build/libc/libcplus_hardened.a >"$D/build_align.log" 2>&1 \
      && timeout 30 "$D/test_align" >"$D/align.out" 2>"$D/align.err" \
      && grep -qF 'ALIGN_TEST 0' "$D/align.out" && [ ! -s "$D/align.err" ]; then
    pass=$((pass + 1));
else fail=$((fail + 1)); echo "SCOPED_MEM_TEST FAIL 16-byte alignment"; tail -8 "$D/build_align.log"; cat "$D/align.out" 2>/dev/null; fi

# Core-memory audit C2: a stale scratch-backed view must be retired at scope
# exit and must not invalidate a fresh owner's token at the reused address.
cases=$((cases + 1))
if timeout 300 ${CPLUS_BIN:-./build/c+} build "$D/scratch_view_owner.cp" --default --no-werror --sanitize=none \
      -I. -o "$D/scratch_view_owner" -- build/libc/libcplus_hardened.a >"$D/build_view_owner.log" 2>&1 \
      && timeout 30 "$D/scratch_view_owner" >"$D/view_owner.out" 2>"$D/view_owner.err" \
      && grep -qF 'SCRATCH_VIEW 0' "$D/view_owner.out" && [ ! -s "$D/view_owner.err" ]; then
    pass=$((pass + 1));
else fail=$((fail + 1)); echo "SCOPED_MEM_TEST FAIL scratch view owner"; tail -8 "$D/build_view_owner.log"; cat "$D/view_owner.out" 2>/dev/null; fi

# Same C2 cross-object proof in a release build (NDEBUG): view retirement is
# unconditional; only the debug poison store is compiled out.
cases=$((cases + 1))
if timeout 300 ${CPLUS_BIN:-./build/c+} build "$D/scratch_view_owner.cp" --default --no-werror --sanitize=none \
      -DNDEBUG -I. -o "$D/scratch_view_owner_release" -- build/libc/libcplus_hardened.a >"$D/build_view_owner_release.log" 2>&1 \
      && timeout 30 "$D/scratch_view_owner_release" >"$D/view_owner_release.out" 2>"$D/view_owner_release.err" \
      && grep -qF 'SCRATCH_VIEW 0' "$D/view_owner_release.out" && [ ! -s "$D/view_owner_release.err" ]; then
    pass=$((pass + 1));
else fail=$((fail + 1)); echo "SCOPED_MEM_TEST FAIL scratch view owner release"; tail -8 "$D/build_view_owner_release.log"; cat "$D/view_owner_release.out" 2>/dev/null; fi

echo "SCOPED_MEM_TEST $cases $pass $fail"
[ "$fail" -eq 0 ]
