#!/bin/sh
# Regression for Agent 1 issues #198, #199 and #201.
#
# Builds issue198_201_unit.cpp against the freshly built cplus_core archive
# (reusing the exact compile/link flags CMake generated) and runs it.  Also
# verifies the STRICT effect gate through the c+ CLI: compiler-generated helper
# use is accepted, user-declared reserved helpers and unverified pointer
# externs are rejected, and builtin SafeCRT externs stay accepted.
#
# Usage: CPLUS_BUILD=/path/to/build-dir CPLUS_BIN=/path/to/c+ \
#        sh tests/compiler/agent1/regression_issues198_201.sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
BUILD=${CPLUS_BUILD:-$ROOT/build-local}
CPLUS=${CPLUS_BIN:-$BUILD/c+}
TESTDIR="$ROOT/tests/compiler/agent1"
WORK=${TMPDIR:-/tmp}/cplus-regression-198-201-$$
mkdir -p "$WORK" || exit 1
trap 'rm -rf "$WORK"' EXIT INT TERM

fail() { echo "FAIL: $*" >&2; exit 1; }

[ -f "$BUILD/compile_commands.json" ] || fail "no compile_commands.json in $BUILD"
[ -f "$BUILD/CMakeFiles/c+.dir/link.txt" ] || fail "no c+ link.txt in $BUILD"
[ -x "$CPLUS" ] || fail "no compiler at $CPLUS"

# Derive the exact C++ compile flags from CMake, then link the unit test with
# the exact c+ link line with Main.cpp.o swapped for the test object.
python3 - "$BUILD" "$TESTDIR/issue198_201_unit.cpp" "$WORK/unit.o" "$WORK/unit" <<'PY'
import json, pathlib, shlex, sys
build, source, obj, out = sys.argv[1:5]
cmds = json.load(open(pathlib.Path(build) / 'compile_commands.json'))
entry = next(e for e in cmds if e['file'].endswith('Sema/TypeChecker.cpp'))
argv = shlex.split(entry['command'])
# Strip -o <object> -c <source> and keep every define/include/flag.
i = argv.index('-o')
del argv[i:i+2]
i = argv.index('-c')
del argv[i:i+2]
argv += ['-o', obj, '-c', source]
open(pathlib.Path(build) / '.issue198_201_compile.sh', 'w').write(
    '#!/bin/sh\ncd ' + shlex.quote(build) + '\n' + shlex.join(argv) + '\n')

link = (pathlib.Path(build) / 'CMakeFiles/c+.dir/link.txt').read_text().strip()
link = link.replace('CMakeFiles/c+.dir/compiler/Main.cpp.o', shlex.quote(obj))
link = link.replace('-o c+', '-o ' + shlex.quote(out))
open(pathlib.Path(build) / '.issue198_201_link.sh', 'w').write(
    '#!/bin/sh\ncd ' + shlex.quote(build) + '\n' + link + '\n')
PY
sh "$BUILD/.issue198_201_compile.sh" || fail "unit test compilation failed"
sh "$BUILD/.issue198_201_link.sh" || fail "unit test link failed"
"$WORK/unit" || fail "unit assertions failed"

# #199 through the real CLI: a user extern with a cplus_ prefix must be
# rejected by STRICT check even though it has no body and no contract.
cat > "$WORK/backdoor.c" <<'EOF'
extern void cplus_unverified_sink(void *p);
void test(int *x) { cplus_unverified_sink(x); }
EOF
if "$CPLUS" check --strict "$WORK/backdoor.c" >"$WORK/backdoor.log" 2>&1; then
  fail "STRICT accepted an unverified cplus_ extern"
fi
grep -qF "[CPLUS-FFI] function 'cplus_unverified_sink'" "$WORK/backdoor.log" ||
  fail "missing STRICT unverified-extern diagnostic"

# #199: helpers EMITTED BY THE COMPILER must stay trusted under STRICT. The
# source below contains no helper declarations; with --sanitize=none the
# backend has no UBSan overflow check, so lowering emits its own guarded
# checked-arithmetic helper calls. `check` still accepts the source, and a
# STRICT build must both succeed and contain real generated call sites (not
# just the extern declarations in the rewriter prelude).
cat > "$WORK/generated.c" <<'EOF'
int add_wrapped(int a, int b) { return a + b; }
int mul_wrapped(int a, int b) { return a * b; }
int main(void) {
  return add_wrapped(2, 3) == 5 && mul_wrapped(4, 5) == 20 ? 0 : 1;
}
EOF
"$CPLUS" check --strict "$WORK/generated.c" >"$WORK/generated-check.log" 2>&1 ||
  { cat "$WORK/generated-check.log" >&2; fail "STRICT check rejected a compiler-helper source"; }
"$CPLUS" build --strict --sanitize=none --emit-c "$WORK/generated-lowered.c" \
  "$WORK/generated.c" -o "$WORK/generated.bin" >"$WORK/generated-build.log" 2>&1 ||
  { cat "$WORK/generated-build.log" >&2; fail "STRICT build rejected compiler-generated helper use"; }
grep -qF '__cplus_inline_add_i64((int64_t)(' "$WORK/generated-lowered.c" ||
  fail "lowered C has no compiler-generated checked-add call"
grep -qF '__cplus_inline_mul_i64((int64_t)(' "$WORK/generated-lowered.c" ||
  fail "lowered C has no compiler-generated checked-mul call"
"$WORK/generated.bin" || fail "generated-helper program failed at run time"

# #199: the SAME helper names are an attack when written by the user. A
# source-local extern can interpose the generated guard at link time, so
# STRICT must reject both a runtime ABI helper and a generated arithmetic
# helper, with the reserved-symbol diagnostic for each.
cat > "$WORK/reserved.c" <<'EOF'
extern void cplus_scan_value_at(void *dest, unsigned long size, int kind,
                                const char *expr, const char *file, int line);
extern long __cplus_checked_add_i64(long a, long b, unsigned bits,
                                    unsigned sgn);
int f(int *p) {
  cplus_scan_value_at(p, sizeof(int), 0, "p", "f", 1);
  return (int)__cplus_checked_add_i64(1, 2, 32, 1);
}
EOF
if "$CPLUS" check --strict "$WORK/reserved.c" >"$WORK/reserved.log" 2>&1; then
  fail "STRICT accepted user declarations of reserved helpers"
fi
for sym in cplus_scan_value_at __cplus_checked_add_i64; do
  grep -qF "user declaration of reserved C+ runtime helper '$sym'" "$WORK/reserved.log" ||
    fail "missing reserved-helper declaration diagnostic for $sym"
  grep -qF "symbol '$sym' is reserved for the C+ compiler/runtime" "$WORK/reserved.log" ||
    fail "missing reserved-symbol diagnostic for $sym"
done

# #198: newly summarised SafeCRT names must be accepted as bodiless externs
# by the STRICT gate (they have authoritative builtin effect summaries).
cat > "$WORK/safecrt.c" <<'EOF'
#include <stddef.h>
extern size_t strncat_s(char *, size_t, const char *, size_t);
extern int memset_s(void *, size_t, int, size_t);
extern int fclose_s(void *);
extern size_t fread_s(void *, size_t, size_t, size_t, void *);
void use(char *d, const char *s, void *f) {
  (void)strncat_s(d, 16, s, 4);
  (void)memset_s(d, 16, 0, 16);
  (void)fclose_s(f);
  (void)fread_s(d, 16, 1, 4, f);
}
EOF
"$CPLUS" check --strict "$WORK/safecrt.c" >"$WORK/safecrt.log" 2>&1 ||
  { cat "$WORK/safecrt.log" >&2; fail "STRICT rejected builtin SafeCRT externs"; }
grep -qF 'unverifiedPointerExterns' "$WORK/safecrt.log" 2>/dev/null && :

echo "issues #198/#199/#201 regression: PASS"
