#!/bin/sh
# Build every native C+ replacement for the Python test tooling.
#
# Each tool is a single self-contained .cp program. Outputs (executables and
# per-tool build logs) go to build/native-tools/. The script is POSIX sh and
# works from any directory; it changes to the repository root before building
# so every printed command is copy-pasteable verbatim.
#
# Design notes:
#   * every tool is built with --strict --sanitize=none (default safe mode);
#   * JSON tools receive the yyjson include/link triplet as three separate
#     argv elements (never one joined string);
#   * tools that execute other programs link the hardened libc archive after
#     "--" to resolve the cplus_system() wrapper;
#   * a missing source file or any compiler error makes the script exit 1.
#     Nothing is fabricated: a tool that was not written yet is reported as
#     MISSING and is not counted as a pass.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 1

CPLUS=${CPLUS_BIN:-./build/c+}
OUT=build/native-tools
mkdir -p "$OUT"

if [ ! -x "$CPLUS" ]; then
  echo "error: C+ driver not executable: $CPLUS (override with CPLUS_BIN)" >&2
  exit 1
fi

BUILT=0
FAILED=0
MISSING=0

# build <name> <source> <need-json:0|1> <need-libc-archive:0|1>
build() {
  name=$1
  src=$2
  need_json=$3
  need_libc=$4
  out=$OUT/$name

  set -- "$CPLUS" build "$src" --strict --sanitize=none
  if [ "$need_json" -eq 1 ]; then
    set -- "$@" -Ithird_party/yyjson -Lbuild/ext -lyyjson
  fi
  set -- "$@" -o "$out"
  if [ "$need_libc" -eq 1 ]; then
    set -- "$@" -- build/libc/libcplus_hardened.a
  fi

  printf '== %s ==\n' "$name"
  printf '+'
  printf ' %s' "$@"
  printf '\n'

  if [ ! -f "$src" ]; then
    printf '  MISSING %s (source not written yet: %s)\n' "$name" "$src"
    MISSING=$((MISSING + 1))
    FAILED=$((FAILED + 1))
    return
  fi

  if "$@" >"$out.build.log" 2>&1; then
    printf '  ok   %s\n' "$name"
    BUILT=$((BUILT + 1))
  else
    rc=$?
    printf '  FAIL %s (compiler rc=%d; see %s)\n' "$name" "$rc" "$out.build.log"
    FAILED=$((FAILED + 1))
  fi
}

echo "root: $ROOT"
echo "driver: $CPLUS"
echo "output: $OUT"
echo

# name, source, JSON/yyjson flags, hardened libc archive (cplus_system)
# JSON = 1 for tools that read or emit JSON; libc = 1 for tools that run
# subprocesses through cplus_system(). See tests/native/README.md.
while read -r name src json libc; do
  if [ -z "$name" ]; then
    continue
  fi
  case "$name" in \#*) continue ;; esac
  build "$name" "$src" "$json" "$libc"
done <<'TOOLS'
validate_contracts        tests/libc/validate_contracts.cp                 1 0
test_compiler_contract_alignment tests/libc/test_compiler_contract_alignment.cp 1 0
test_contract_consumption tests/libc/test_contract_consumption.cp         1 1
test_contract_integrity   tests/libc/test_contract_integrity.cp           1 1
coverage                  tests/stdlib/coverage.cp                        0 0
run_adversarial           tests/stdlib/run_adversarial.cp                 1 1
run_all                   tests/stdlib/agent2/run_all.cp                  1 1
fuzz100_run               tests/fuzz100/run.cp                            1 1
TOOLS

echo
printf 'native tools: %d built, %d failed, %d missing\n' "$BUILT" "$FAILED" "$MISSING"
[ "$FAILED" -eq 0 ]
