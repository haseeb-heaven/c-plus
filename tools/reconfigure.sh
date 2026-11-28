#!/bin/sh
# Reconfigure a C+ build directory, recovering from a stale CMakeCache.txt
# that points at a different source tree (a common worktree/rename issue).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD=${1:-$ROOT/build}

if [ -f "$BUILD/CMakeCache.txt" ]; then
  cached=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD/CMakeCache.txt" | head -1)
  if [ -n "$cached" ] && [ "$cached" != "$ROOT" ]; then
    echo "stale CMake cache: $cached -> $ROOT"
    mv "$BUILD" "${BUILD}.stale-$(date +%Y%m%d-%H%M%S)"
  fi
fi

LLVM_PREFIX=${LLVM_PREFIX:-}
if [ -z "$LLVM_PREFIX" ] && command -v brew >/dev/null 2>&1; then
  # `brew --prefix llvm` can report an unlinked path; prefer the Cellar
  # directory that actually contains the CMake package files.
  for candidate in $(brew --cellar llvm 2>/dev/null)/*; do
    if [ -d "$candidate/lib/cmake/llvm" ]; then
      LLVM_PREFIX="$candidate"
      break
    fi
  done
fi

args=""
if [ -n "$LLVM_PREFIX" ]; then
  args="-DLLVM_DIR=$LLVM_PREFIX/lib/cmake/llvm -DClang_DIR=$LLVM_PREFIX/lib/cmake/clang"
fi

# shellcheck disable=SC2086
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON $args
echo "configured $BUILD from $ROOT"
