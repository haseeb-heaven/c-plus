#!/bin/sh
# tools/build-linux.sh -- reproducible Linux build driver for C+.
#
# This script is deliberately honest: a status is PASS only when the command
# for that row actually ran successfully in this invocation.  Nothing is
# inferred from a successful configure, a cached binary, or a container that
# merely exists.  BLOCKED means the command was attempted and failed; the
# captured reason is printed.  NOT_TESTED means no attempt was made.
#
# Usage:
#   sh tools/build-linux.sh probe                 # read-only capability report
#   sh tools/build-linux.sh cross   x86_64         # cross attempt from macOS
#   sh tools/build-linux.sh cross   aarch64
#   sh tools/build-linux.sh native               # build in a Linux host/container
#
# Environment overrides:
#   CPLUS_LINUX_SYSROOT   sysroot root (else auto-discovered)
#   LLVM_DIR, Clang_DIR   CMake package dirs (else auto-discovered)
#   BUILD_DIR             build tree (default: <repo>/build-linux-<arch>)
#   LLVM_CONFIG           llvm-config executable (version-suffixed OK)
#
# Cross-compiling the *compiler* requires LLVM/Clang static archives compiled
# for the *target* ELF architecture.  A macOS Homebrew LLVM is a Mach-O arm64
# build: it can emit ELF code via --target=..., but its .a files cannot be
# linked into an ELF executable.  This script detects that and reports BLOCKED
# with the exact file(1) evidence instead of pretending the step passed.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ARCH=${2:-x86_64}
MODE=${1:-probe}
BUILD_DIR=${BUILD_DIR:-$ROOT/build-linux-$ARCH}

S_X86=NOT_TESTED; S_ARM=NOT_TESTED; S_TOOLCHAIN=NOT_TESTED
S_SYSROOT=NOT_TESTED; S_LLVM=NOT_TESTED; S_NATIVE=NOT_TESTED
S_TESTS=NOT_TESTED; S_ABI=NOT_TESTED
REASON_X86=""; REASON_ARM=""; REASON_LLVM=""; REASON_NATIVE=""

note() { printf '%s\n' "$*"; }
hr() { printf '%s\n' '--------------------------------------------------------------------------'; }

triple_for() {
  case "$1" in
    x86_64|amd64)   printf 'x86_64-unknown-linux-gnu' ;;
    aarch64|arm64)  printf 'aarch64-unknown-linux-gnu' ;;
    *) printf '%s-unknown-linux-gnu' "$1" ;;
  esac
}
TRIPLE=$(triple_for "$ARCH")

# --- sysroot discovery -----------------------------------------------------
discover_sysroot() {
  t=$1
  for c in \
    "${CPLUS_LINUX_SYSROOT:-}" \
    "${SYSROOT:-}" \
    "/opt/sysroot/$t" \
    "/usr/$t" \
    "/opt/homebrew/sysroot/$t" \
    "/opt/homebrew/opt/sysroot/$t" \
    "/opt/osxcross/sysroot/$t" \
    "$ROOT/.sysroot/$t"; do
    if [ -n "$c" ] && [ -d "$c" ] && [ -d "$c/usr/include" ]; then
      printf '%s' "$c"; return 0
    fi
  done
  return 1
}

# --- LLVM CMake package discovery -----------------------------------------
llvm_package_dirs() {
  if [ -n "${LLVM_DIR:-}" ] && [ -n "${Clang_DIR:-}" ]; then
    printf '%s|%s' "$LLVM_DIR" "$Clang_DIR"; return 0
  fi
  cfg=${LLVM_CONFIG:-}
  if [ -z "$cfg" ]; then
    for c in llvm-config llvm-config-23 llvm-config-22 llvm-config-18 llvm-config-15; do
      command -v "$c" >/dev/null 2>&1 && { cfg=$c; break; }
    done
  fi
  if [ -n "$cfg" ] && command -v "$cfg" >/dev/null 2>&1; then
    p=$($cfg --prefix 2>/dev/null) || return 1
    if [ -f "$p/lib/cmake/llvm/LLVMConfig.cmake" ] && \
       [ -f "$p/lib/cmake/clang/ClangConfig.cmake" ]; then
      printf '%s/lib/cmake/llvm|%s/lib/cmake/clang' "$p" "$p"; return 0
    fi
  fi
  for p in /opt/homebrew/opt/llvm /usr/lib/llvm-23 /usr/lib/llvm-22 \
           /usr/lib/llvm-18 /usr/lib/llvm-15 /usr/local/opt/llvm; do
    if [ -f "$p/lib/cmake/llvm/LLVMConfig.cmake" ] && \
       [ -f "$p/lib/cmake/clang/ClangConfig.cmake" ]; then
      printf '%s/lib/cmake/llvm|%s/lib/cmake/clang' "$p" "$p"; return 0
    fi
  done
  return 1
}

# Detect whether the LLVM archives we would link are ELF objects for the
# requested target.  Mach-O (or any non-ELF) archives cannot produce an ELF
# cross-compiled compiler: this is the exact blocker, printed verbatim.
check_llvm_archives() {
  pkgs=$1; t=$2
  llvm_dir=${pkgs%%|*}
  libdir=$(dirname "$(dirname "$llvm_dir")")
  case "$t" in
    x86_64*) want='ELF 64-bit.*x86-64' ;;
    aarch64*) want='ELF 64-bit.*ARM aarch64' ;;
    *) want='ELF' ;;
  esac
  archive=$(find "$libdir" -maxdepth 1 -name 'libLLVMCore.a' 2>/dev/null | head -1)
  if [ -z "$archive" ] || [ ! -f "$archive" ]; then
    REASON_LLVM="no libLLVMCore.a under $libdir"
    return 1
  fi
  obj=$(cd "$ROOT" && ar t "$archive" 2>/dev/null | grep '\.o$' | head -1)
  if [ -z "$obj" ]; then
    REASON_LLVM="cannot list an object member of $archive"
    return 1
  fi
  tmp=$(mktemp -d "${TMPDIR:-/tmp}/cplus-linux-llvm.XXXXXX") || return 1
  (cd "$tmp" && ar x "$archive" "$obj" 2>/dev/null)
  desc=$(file -b "$tmp/$obj" 2>/dev/null || printf 'unknown')
  rm -rf "$tmp"
  printf 'LLVM archive member %s: %s\n' "$obj" "$desc"
  case "$desc" in
    *ELF*) printf '%s' "$desc" | grep -Eq "$want" && return 0 ;;
  esac
  REASON_LLVM="LLVM static archives are '$desc', not ELF for $t; cannot link a $t C+ compiler from this toolchain"
  return 1
}

report() {
  hr
  note 'C+ Linux build status (PASS = command actually ran successfully here)'
  hr
  printf '%-44s | %-11s | %s\n' 'ITEM' 'STATUS' 'EVIDENCE / REASON'
  hr
  printf '%-44s | %-11s | %s\n' "toolchain host ($(uname -s)/$(uname -m))" "$S_TOOLCHAIN" "$(command -v clang 2>/dev/null || echo clang-not-found)"
  printf '%-44s | %-11s | %s\n' 'sysroot discovery' "$S_SYSROOT" "${SYSROOT_PATH:-none found}"
  printf '%-44s | %-11s | %s\n' "LLVM archives for $TRIPLE" "$S_LLVM" "${REASON_LLVM:-ELF target archives present}"
  printf '%-44s | %-11s | %s\n' 'cross build x86_64 ELF compiler' "$S_X86" "${REASON_X86:-not attempted}"
  printf '%-44s | %-11s | %s\n' 'cross build aarch64 ELF compiler' "$S_ARM" "${REASON_ARM:-not attempted}"
  printf '%-44s | %-11s | %s\n' 'native/container build' "$S_NATIVE" "${REASON_NATIVE:-not attempted}"
  printf '%-44s | %-11s | %s\n' 'ctest suite' "$S_TESTS" 'not attempted'
  printf '%-44s | %-11s | %s\n' 'C ABI layout check' "$S_ABI" 'not attempted (no built binary)'
  hr
}

command -v clang >/dev/null 2>&1 && S_TOOLCHAIN=PASS
SYSROOT_PATH=$(discover_sysroot "$TRIPLE" || true)
[ -n "$SYSROOT_PATH" ] && S_SYSROOT=PASS

case "$MODE" in
  probe)
    S_X86=NOT_TESTED; S_ARM=NOT_TESTED
    S_LLVM=NOT_TESTED; REASON_LLVM='probe mode: archive architecture not inspected'
    ;;
  cross)
    if pkgs=$(llvm_package_dirs); then
      if check_llvm_archives "$pkgs" "$TRIPLE"; then
        S_LLVM=PASS
        # Only attempt CMake when the archives can actually produce target ELF.
        if [ -n "$SYSROOT_PATH" ]; then
          # shellcheck disable=SC2086
          cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_SYSTEM_NAME=Linux \
            -DCMAKE_SYSTEM_PROCESSOR=$(printf '%s' "$TRIPLE" | cut -d- -f1) \
            -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
            -DCMAKE_C_COMPILER_TARGET="$TRIPLE" \
            -DCMAKE_CXX_COMPILER_TARGET="$TRIPLE" \
            -DCMAKE_SYSROOT="$SYSROOT_PATH" \
            -DLLVM_DIR="${pkgs%%|*}" -DClang_DIR="${pkgs##*|}" \
            -DLLVM_LINK_LLVM_DYLIB=OFF -DLLVM_BUILD_LLVM_DYLIB=OFF \
            && cmake --build "$BUILD_DIR" --target c+ --parallel \
            && { [ "$ARCH" = x86_64 ] && S_X86=PASS || S_ARM=PASS; } \
            || { reason="cmake configure/build failed; see $BUILD_DIR"; \
                 [ "$ARCH" = x86_64 ] && REASON_X86=$reason || REASON_ARM=$reason; }
        else
          reason="no $TRIPLE sysroot found (set CPLUS_LINUX_SYSROOT)"
          [ "$ARCH" = x86_64 ] && REASON_X86=$reason || REASON_ARM=$reason
        fi
      else
        S_LLVM=BLOCKED
        reason=$REASON_LLVM
        [ "$ARCH" = x86_64 ] && { S_X86=BLOCKED; REASON_X86=$reason; } \
                              || { S_ARM=BLOCKED; REASON_ARM=$reason; }
      fi
    else
      reason='LLVM/Clang CMake package dirs not found (set LLVM_DIR and Clang_DIR)'
      [ "$ARCH" = x86_64 ] && { S_X86=BLOCKED; REASON_X86=$reason; } \
                            || { S_ARM=BLOCKED; REASON_ARM=$reason; }
    fi
    ;;
  native)
    if [ "$(uname -s)" != Linux ]; then
      S_NATIVE=BLOCKED
      REASON_NATIVE='native mode requires a Linux host/container; use "cross" from macOS'
    elif pkgs=$(llvm_package_dirs); then
      # shellcheck disable=SC2086
      if cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
           -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
           -DLLVM_DIR="${pkgs%%|*}" -DClang_DIR="${pkgs##*|}" \
           -DLLVM_LINK_LLVM_DYLIB=OFF -DLLVM_BUILD_LLVM_DYLIB=OFF \
         && cmake --build "$BUILD_DIR" --target c+ cplus_runtime --parallel; then
        S_NATIVE=PASS
        if ctest --test-dir "$BUILD_DIR" --output-on-failure; then S_TESTS=PASS
        else S_TESTS=BLOCKED; fi
      else
        S_NATIVE=BLOCKED; REASON_NATIVE="configure/build failed; see $BUILD_DIR"
      fi
    else
      S_NATIVE=BLOCKED; REASON_NATIVE='LLVM/Clang CMake package dirs not found'
    fi
    ;;
  *)
    note "unknown mode: $MODE (expected probe|cross|native)"; report; exit 2 ;;
esac

report
# Never claim success unless the requested build itself passed.
case "$MODE" in
  cross) [ "$ARCH" = x86_64 ] && [ "$S_X86" = PASS ] && exit 0 || exit 1 ;;
  native) [ "$S_NATIVE" = PASS ] && exit 0 || exit 1 ;;
esac
exit 0
