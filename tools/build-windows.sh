#!/bin/sh
# tools/build-windows.sh -- reproducible C+ Windows cross-build / verification.
#
# Targets: TARGET=x86_64 (default) or TARGET=aarch64.
#
# This driver NEVER executes a Windows binary. It prints an explicit status for
# every stage: PASS (artifact verified), BLOCKED (exact toolchain reason given),
# or NOT_TESTED (prerequisite absent). Nothing is silently skipped and no
# compiler diagnostic is suppressed.
#
# Usage:
#   sh tools/build-windows.sh                 # probe + compile all sources
#   TARGET=aarch64 sh tools/build-windows.sh  # ARM64 Windows target
#   sh tools/build-windows.sh --link          # also attempt the final c+.exe link
#
# Optional overrides:
#   TARGET, CLANGXX, LLVM_CONFIG, LLVM_PREFIX, MINGW_PREFIX, SYSROOT,
#   AARCH64_SYSROOT, BUILD_DIR.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET=${TARGET:-x86_64}
CLANGXX=${CLANGXX:-clang++}
CLANG=${CLANG:-clang}
BUILD_DIR=${BUILD_DIR:-$ROOT/build-windows-$TARGET}
LINK=0
[ "${1:-}" = "--link" ] && LINK=1

HARD_BLOCK=0
OK_COUNT=0
FAIL_COUNT=0

say() { printf '%s\n' "$*"; }
pass() { OK_COUNT=$((OK_COUNT + 1)); printf 'PASS  %s\n' "$*"; }
blocked() { HARD_BLOCK=$((HARD_BLOCK + 1)); printf 'BLOCKED  %s\n' "$*"; }
not_tested() { printf 'NOT_TESTED  %s\n' "$*"; }
fail() { printf 'FAIL  %s\n' "$*" >&2; exit 1; }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/cplus-winbuild.XXXXXX") || fail "mktemp failed"
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

say "== host =="
uname -a
say "source: $ROOT"
say "target: $TARGET"

# ---------------------------------------------------------------------------
# 1. Toolchain discovery. Each missing prerequisite is recorded, never guessed.
# ---------------------------------------------------------------------------
command -v "$CLANGXX" >/dev/null 2>&1 || fail "C++ compiler not found: $CLANGXX"
command -v "$CLANG" >/dev/null 2>&1 || fail "C compiler not found: $CLANG"
"$CLANGXX" --version | sed -n '1p'

LLVM_CONFIG=${LLVM_CONFIG:-}
if [ -z "$LLVM_CONFIG" ] && command -v llvm-config >/dev/null 2>&1; then
  LLVM_CONFIG=$(command -v llvm-config)
fi
if [ -z "$LLVM_CONFIG" ] && [ -n "${LLVM_PREFIX:-}" ] && [ -x "$LLVM_PREFIX/bin/llvm-config" ]; then
  LLVM_CONFIG="$LLVM_PREFIX/bin/llvm-config"
fi
if [ -z "$LLVM_CONFIG" ] && command -v brew >/dev/null 2>&1; then
  bp=$(brew --prefix llvm 2>/dev/null || true)
  if [ -n "$bp" ] && [ -x "$bp/bin/llvm-config" ]; then LLVM_CONFIG="$bp/bin/llvm-config"; fi
fi
[ -n "$LLVM_CONFIG" ] && [ -x "$LLVM_CONFIG" ] || fail "llvm-config not found; set LLVM_CONFIG or LLVM_PREFIX"
LLVM_PREFIX=$("$LLVM_CONFIG" --prefix)
LLVM_INCLUDE_DIR=$("$LLVM_CONFIG" --includedir)
LLVM_LIBDIR=$("$LLVM_CONFIG" --libdir)
say "LLVM: $("$LLVM_CONFIG" --version) from $LLVM_PREFIX"
[ -d "$LLVM_INCLUDE_DIR/clang" ] || fail "LLVM/Clang C++ headers missing: $LLVM_INCLUDE_DIR"

case "$TARGET" in
  x86_64)  TRIPLE=x86_64-w64-windows-gnu;  PREFIX=x86_64-w64-mingw32 ;;
  aarch64) TRIPLE=aarch64-w64-windows-gnu; PREFIX=aarch64-w64-mingw32 ;;
  *) fail "unsupported TARGET=$TARGET (use x86_64 or aarch64)" ;;
esac

# Confirm the driver actually has this backend before blaming missing headers.
if "$CLANG" --target="$TRIPLE" -dM -E -x c /dev/null 2>/dev/null | grep -q '^#define _WIN32 1'; then
  pass "clang backend accepts target triple $TRIPLE"
else
  fail "clang does not recognise target triple $TRIPLE"
fi

# ---------------------------------------------------------------------------
# 2. Windows sysroot. No sysroot == no CRT/C++ headers == no real build.
# ---------------------------------------------------------------------------
SYSROOT=${SYSROOT:-}
if [ -z "$SYSROOT" ] && [ "$TARGET" = x86_64 ] && [ -n "${MINGW_PREFIX:-}" ]; then
  SYSROOT=$MINGW_PREFIX
fi
if [ -z "$SYSROOT" ] && command -v "${PREFIX}-gcc" >/dev/null 2>&1; then
  SYSROOT=$("${PREFIX}-gcc" -print-sysroot)
fi
if [ -z "$SYSROOT" ] && [ "$TARGET" = x86_64 ] && command -v brew >/dev/null 2>&1; then
  mp=$(brew --prefix mingw-w64 2>/dev/null || true)
  if [ -n "$mp" ] && [ -d "$mp/toolchain-x86_64" ]; then SYSROOT="$mp/toolchain-x86_64"; fi
fi

HAVE_SYSROOT=0
if [ -n "$SYSROOT" ] && [ -d "$SYSROOT/$PREFIX/include" ]; then
  HAVE_SYSROOT=1
  pass "Windows sysroot: $SYSROOT"
else
  blocked "no $TARGET Windows sysroot (SYSROOT=$SYSROOT, prefix=$PREFIX/include)"
  say "  reason: ${PREFIX}-gcc is absent or no CRT header directory exists"
fi

case "$LLVM_INCLUDE_DIR$SYSROOT$ROOT" in
  *" "*) fail "this script requires paths without spaces: $LLVM_INCLUDE_DIR / $SYSROOT / $ROOT" ;;
esac

# ---------------------------------------------------------------------------
# 3. Minimal cross-compilation probes (compile only; never run the artifact).
# ---------------------------------------------------------------------------
cat > "$TMP/probe.c" <<'EOF'
#include <stdio.h>
int main(void) { puts("probe"); return 0; }
EOF
cat > "$TMP/probe.cpp" <<'EOF'
#include <string>
int main() { std::string s = "probe"; return s.size() == 5 ? 0 : 1; }
EOF

if [ "$HAVE_SYSROOT" -eq 1 ]; then
  if "$CLANG" --target="$TRIPLE" --sysroot="$SYSROOT" -c "$TMP/probe.c" -o "$TMP/probe_c.o" >"$TMP/probe_c.log" 2>&1; then
    pass "C -> COFF object ($TRIPLE): $(file -b "$TMP/probe_c.o")"
  else
    blocked "C -> COFF object failed"; sed -n '1,6p' "$TMP/probe_c.log"
  fi
  if "$CLANGXX" --target="$TRIPLE" --sysroot="$SYSROOT" -c "$TMP/probe.cpp" -o "$TMP/probe_cpp.o" >"$TMP/probe_cpp.log" 2>&1; then
    pass "C++ -> COFF object ($TRIPLE): $(file -b "$TMP/probe_cpp.o")"
  else
    blocked "C++ -> COFF object failed"; sed -n '1,6p' "$TMP/probe_cpp.log"
  fi
else
  # A backend can still emit COFF without a sysroot for freestanding code.
  printf 'int add(int a, int b) { return a + b; }\n' > "$TMP/free.c"
  if "$CLANG" --target="$TRIPLE" -c "$TMP/free.c" -o "$TMP/free.o" >"$TMP/free.log" 2>&1; then
    pass "freestanding $TRIPLE codegen only: $(file -b "$TMP/free.o")"
  else
    not_tested "freestanding $TRIPLE codegen"; sed -n '1,4p' "$TMP/free.log"
  fi
  # Show the exact C++ header blocker instead of a bare "unsupported".
  printf '#include <string>\nstd::string s = "x";\n' > "$TMP/free.cpp"
  if "$CLANGXX" --target="$TRIPLE" -stdlib=libc++ -isystem "$LLVM_INCLUDE_DIR/c++/v1" -c "$TMP/free.cpp" -o "$TMP/free_cpp.o" >"$TMP/free_cpp.log" 2>&1; then
    pass "host libc++ headers usable for $TRIPLE"
  else
    blocked "host libc++ headers are not usable for $TRIPLE"
    sed -n '1,5p' "$TMP/free_cpp.log"
  fi
fi

# ---------------------------------------------------------------------------
# 4. Compile the compiler's C++ translation units as far as possible.
#    Flags mirror CMakeLists.txt (C++20, -fno-rtti, LLVM/Clang includes).
# ---------------------------------------------------------------------------
OBJ_DIR=$BUILD_DIR/obj
mkdir -p "$OBJ_DIR"
set -- --target="$TRIPLE" --sysroot="$SYSROOT" -std=c++20 -fno-rtti \
  -isystem "$LLVM_INCLUDE_DIR" \
  -I"$ROOT/compiler" -I"$ROOT/runtime/include" \
  '-DCLANG_CPLUS_LANG_STRING="C+"' '-DCPLUS_VERSION_STRING="0.2.0"'

if [ "$HAVE_SYSROOT" -eq 0 ]; then
  not_tested "compiler source compilation (no usable sysroot for $TRIPLE)"
else
  say "== compile compiler sources =="
  SOURCES="compiler/Diagnostics.cpp
compiler/SourceManager.cpp
compiler/Analysis/OwnershipAnalysis.cpp
compiler/Sema/SemaCPlus.cpp
compiler/Sema/CPlusLexer.cpp
compiler/Sema/CPlusRewriter.cpp
compiler/Sema/RaiiLowering.cpp
compiler/Sema/StaticBoundsCheck.cpp
compiler/Sema/AstSafetyCheck.cpp
compiler/Sema/TypeChecker.cpp
runtime/CPlusRuntime.cpp
compiler/Driver/Driver.cpp
compiler/Main.cpp"
  for f in $SOURCES; do
    [ -f "$ROOT/$f" ] || fail "missing source listed in CMakeLists.txt: $f"
    base=$(printf '%s' "$f" | tr '/' '_')
    obj="$OBJ_DIR/${base%.cpp}.o"
    if (cd "$ROOT" && "$CLANGXX" "$@" -c "$f" -o "$obj") >"$TMP/one.log" 2>&1; then
      OK_COUNT=$((OK_COUNT + 1))
      printf 'PASS  compile %s\n' "$f"
    else
      FAIL_COUNT=$((FAIL_COUNT + 1))
      HARD_BLOCK=$((HARD_BLOCK + 1))
      printf 'BLOCKED  compile %s: ' "$f"
      grep -m1 'error:' "$TMP/one.log" || sed -n '1p' "$TMP/one.log"
    fi
  done
fi

# ---------------------------------------------------------------------------
# 5. Check the LLVM archives the final link would require. Host-built LLVM
#    archives are Mach-O and cannot be linked into a PE executable.
# ---------------------------------------------------------------------------
say "== link prerequisites =="
SUPPORT_LIB="$LLVM_LIBDIR/libLLVMSupport.a"
if [ -f "$SUPPORT_LIB" ]; then
  member=$(ar t "$SUPPORT_LIB" 2>/dev/null | grep '\.o$' | head -1 || true)
  if [ -n "$member" ]; then
    (cd "$TMP" && ar x "$SUPPORT_LIB" "$member") >/dev/null 2>&1 || true
    if [ -f "$TMP/$member" ]; then
      fmt=$(file -b "$TMP/$member")
      case "$fmt" in
        *Mach-O*) blocked "LLVM archive member $member is host format: $fmt" ;;
        *COFF*)   pass "LLVM archive member format: $fmt" ;;
        *)        not_tested "unrecognised LLVM archive format: $fmt" ;;
      esac
    else
      not_tested "could not extract $member from $SUPPORT_LIB"
    fi
  else
    not_tested "no .o member found in $SUPPORT_LIB"
  fi
else
  not_tested "LLVM archive missing: $SUPPORT_LIB"
fi

# ---------------------------------------------------------------------------
# 6. Optional final link. Expected to be BLOCKED on a macOS-host LLVM.
# ---------------------------------------------------------------------------
if [ "$LINK" -eq 1 ]; then
  say "== final link =="
  if [ "$FAIL_COUNT" -gt 0 ] || [ "$HAVE_SYSROOT" -eq 0 ]; then
    blocked "cannot link: $FAIL_COUNT source(s) failed, sysroot=$HAVE_SYSROOT"
  else
    OUT=$BUILD_DIR/c-plus.exe
    mkdir -p "$BUILD_DIR"
    if "$CLANGXX" "$@" "$OBJ_DIR"/*.o -o "$OUT" >"$TMP/link.log" 2>&1; then
      pass "linked $OUT ($(file -b "$OUT"))"
    else
      blocked "final c+.exe link failed (see compiler diagnostics below)"
      sed -n '1,12p' "$TMP/link.log"
    fi
  fi
else
  not_tested "final c+.exe link (pass --link to attempt)"
fi

say "== summary =="
say "PASS stages: $OK_COUNT"
say "BLOCKED stages: $HARD_BLOCK"
say "compile failures: $FAIL_COUNT"
say "FULL WINDOWS BUILD: NOT_TESTED"
say "RUNTIME: NOT_TESTED (no Windows executable was executed)"
[ "$HARD_BLOCK" -eq 0 ] || say "See docs/WINDOWS_CROSS_BUILD.md for exact blockers and unblock criteria."
exit 0
