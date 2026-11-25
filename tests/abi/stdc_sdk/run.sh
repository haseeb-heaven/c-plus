#!/bin/sh
# tests/abi/stdc_sdk/run.sh
#
# Reproduce every claim in RESULTS.md. Run from anywhere:
#     sh tests/abi/stdc_sdk/run.sh
#
# Scope: macOS arm64 / Apple clang 16 (the /usr/bin/clang on this host). Set
# CPLUS_CLANG to override (e.g. the scripts default to `clang` on PATH, which
# on this host is Homebrew clang 23 with a broken sysroot -- see RESULTS.md).
#
# Strict ownership: everything this script writes lives under
# tests/abi/stdc_sdk/out/. The two scripts invoked below rebuild the
# gitignored build/libc-cmake and build/libc-install directories; that is
# their own documented behaviour.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"

HERE=tests/abi/stdc_sdk
OUT="$HERE/out"
mkdir -p "$OUT"
ulimit -c 0 2>/dev/null || true

CC=${CPLUS_CLANG:-/usr/bin/clang}
export CPLUS_CLANG="$CC"
INST="$ROOT/build/libc-install"
SAFE="$ROOT/build/safe/libcplus_safecrt.a"
LIBC="$ROOT/build/libc/libcplus_hardened.a"

INC="-I. -Istdlib/safe -Ilibc -Ilibc/install-include -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
LIBS="$SAFE $LIBC -lm -pthread"

say() { printf '%s\n' "$*"; }

say "=== toolchain ==="
say "CC=$CC"
"$CC" --version | head -1
uname -m
say ""

# ---------------------------------------------------------------------------
# 1. The two required repository scripts (capture final line + exit code).
# ---------------------------------------------------------------------------
say "=== required script: tests/sdk/run_sdk_consumer.sh ==="
sh tests/sdk/run_sdk_consumer.sh >"$OUT/sdk_consumer.log" 2>&1
rc=$?
say "exit=$rc"
say "final line: $(tail -n 1 "$OUT/sdk_consumer.log")"
say ""

say "=== required script: tests/libc/install_smoke.sh ==="
sh tests/libc/install_smoke.sh >"$OUT/install_smoke.log" 2>&1
rc=$?
say "exit=$rc"
say "final line: $(tail -n 1 "$OUT/install_smoke.log")"
say ""

[ -f "$INST/lib/libcplus_hardened_libc.a" ] || { say "FATAL: install prefix missing"; exit 1; }

# ---------------------------------------------------------------------------
# 2. Plain standard-C consumer against the CLEAN install prefix.
# ---------------------------------------------------------------------------
say "=== plain consumer: installed headers + installed STATIC library ==="
cmd="$CC -std=c99 -Wall -Wextra -I$INST/include/cplus-libc $HERE/consumer.c $INST/lib/libcplus_hardened_libc.a -lm -pthread -o $OUT/consumer.install-static"
say "+ $cmd"
# shellcheck disable=SC2086
$CC -std=c99 -Wall -Wextra -I"$INST/include/cplus-libc" "$HERE/consumer.c" \
    "$INST/lib/libcplus_hardened_libc.a" -lm -pthread \
    -o "$OUT/consumer.install-static" >"$OUT/plain-static.build.log" 2>&1
say "compile exit=$?"

"$OUT/consumer.install-static" >"$OUT/plain-static.stdout" 2>"$OUT/plain-static.stderr"
prc=$?
say "run exit=$prc"
say "stdout: $(cat "$OUT/plain-static.stdout")"
say "stderr bytes: $(wc -c <"$OUT/plain-static.stderr" | tr -d ' ')"
say ""

say "=== plain consumer: installed headers + installed SHARED library ==="
cmd="$CC -std=c99 -Wall -Wextra -I$INST/include/cplus-libc $HERE/consumer.c -L$INST/lib -lcplus_hardened_libc -Wl,-rpath,$INST/lib -lm -pthread -o $OUT/consumer.install-shared"
say "+ $cmd"
# shellcheck disable=SC2086
$CC -std=c99 -Wall -Wextra -I"$INST/include/cplus-libc" "$HERE/consumer.c" \
    -L"$INST/lib" -lcplus_hardened_libc -Wl,-rpath,"$INST/lib" -lm -pthread \
    -o "$OUT/consumer.install-shared" >"$OUT/plain-shared.build.log" 2>&1
say "compile exit=$?"

"$OUT/consumer.install-shared" >"$OUT/plain-shared.stdout" 2>"$OUT/plain-shared.stderr"
src=$?
say "run exit=$src"
say "stdout: $(cat "$OUT/plain-shared.stdout")"
cmp -s "$OUT/plain-static.stdout" "$OUT/plain-shared.stdout" \
    && say "static vs shared stdout: BYTE-IDENTICAL" \
    || say "static vs shared stdout: DIFFER"
say ""

# ---------------------------------------------------------------------------
# 3. SAME source + checked layer.
# ---------------------------------------------------------------------------
say "=== checked consumer: -include safe_c.h + checked archives ==="
cmd="$CC -std=c99 -Wall -Wextra -include stdlib/safe/safe_c.h $INC $HERE/consumer.c $LIBS -o $OUT/consumer.checked"
say "+ $cmd"
# shellcheck disable=SC2086
$CC -std=c99 -Wall -Wextra -include stdlib/safe/safe_c.h $INC "$HERE/consumer.c" \
    $LIBS -o "$OUT/consumer.checked" >"$OUT/checked.build.log" 2>&1
ccrc=$?
say "compile exit=$ccrc"
[ "$ccrc" -eq 0 ] || { cat "$OUT/checked.build.log"; exit 1; }

"$OUT/consumer.checked" >"$OUT/checked.stdout" 2>"$OUT/checked.stderr"
crc=$?
say "valid run exit=$crc"
say "stdout: $(cat "$OUT/checked.stdout")"
say "stderr bytes: $(wc -c <"$OUT/checked.stderr" | tr -d ' ')"
if cmp -s "$OUT/plain-static.stdout" "$OUT/checked.stdout"; then
    say "plain(install) vs checked stdout: BYTE-IDENTICAL"
else
    say "plain(install) vs checked stdout: DIFFER"
    diff -u "$OUT/plain-static.stdout" "$OUT/checked.stdout" || true
fi
say ""

say "=== hostile case: 40-byte memcpy into an 8-byte object (arg 'x') ==="
"$OUT/consumer.checked" x >"$OUT/checked-hostile.stdout" 2>"$OUT/checked-hostile.stderr"
hrc=$?
say "checked hostile exit=$hrc"
say "checked hostile stdout: $(cat "$OUT/checked-hostile.stdout")"
say "checked hostile stderr: $(cat "$OUT/checked-hostile.stderr")"
EXPECTED='[CPLUS-SAFE] memcpy_s: buffer overflow refused (requested 40 > capacity 8, ESNOSPC 406)'
if [ "$hrc" -eq 70 ] && grep -Fxq "$EXPECTED" "$OUT/checked-hostile.stderr"; then
    say "refusal: PASS (exit 70 + exact ESNOSPC 406 diagnostic)"
else
    say "refusal: FAIL (expected exit 70 + exact diagnostic)"
fi
# Plain build has no check; run in a subshell so a crash cannot kill this script.
( "$OUT/consumer.install-static" x >"$OUT/plain-hostile.stdout" 2>"$OUT/plain-hostile.stderr" )
phrc=$?
say "plain hostile exit=$phrc (UB; no refusal expected)"
say "plain hostile stdout: $(cat "$OUT/plain-hostile.stdout")"
say "plain hostile stderr: $(cat "$OUT/plain-hostile.stderr")"
say ""

# ---------------------------------------------------------------------------
# 4. nm evidence: exported symbol names are unmangled C ABI names.
# ---------------------------------------------------------------------------
say "=== nm: exported symbols of the installed libraries ==="
nm -g --defined-only "$INST/lib/libcplus_hardened_libc.a" >"$OUT/nm-install-static.txt" 2>&1
nm -g --defined-only "$INST/lib/libcplus_hardened_libc.dylib" >"$OUT/nm-install-shared.txt" 2>&1
for s in cplus_malloc cplus_free cplus_memcpy cplus_strcpy cplus_printf; do
    hit=$(grep -E " T _${s}$" "$OUT/nm-install-static.txt" | head -1)
    [ -n "$hit" ] && say "  static: $hit" || say "  static: MISSING _$s"
done
for s in cplus_malloc cplus_free cplus_memcpy cplus_strcpy cplus_printf; do
    hit=$(grep -E " T _${s}$" "$OUT/nm-install-shared.txt" | head -1)
    [ -n "$hit" ] && say "  shared: $hit" || say "  shared: MISSING _$s"
done
mangled=$(grep -cE '(_Z|_ZN|_NK|[?@$])' "$OUT/nm-install-static.txt" || true)
say "C++-mangled-looking symbols in static archive: $mangled"
say "total defined globals (static archive): $(wc -l <"$OUT/nm-install-static.txt" | tr -d ' ')"
say "total defined globals (shared dylib):   $(wc -l <"$OUT/nm-install-shared.txt" | tr -d ' ')"
say ""
say "=== done; raw artifacts in $OUT ==="
