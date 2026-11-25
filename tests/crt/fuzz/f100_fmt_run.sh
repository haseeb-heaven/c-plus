#!/bin/sh
# Deterministic format + I/O fuzz configs F-041..F-060.
# Each harness is a standalone program: fixed-seed LCG, >=1000 operations,
# compiled with /usr/bin/clang + UBSan, executed under a 30s timeout.
# No repository files are written; binaries and logs live in a mktemp dir.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT" || exit 1

CC=/usr/bin/clang
INC="-Istdlib/safe -Ilibc/install-include -Ilibc -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
LIBS="build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a"
SAN="-fsanitize=undefined -fno-sanitize-recover=all -fno-sanitize=float-divide-by-zero"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/f100_fmtio.XXXXXX") || exit 1
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

pass=0; fail=0
CONFIGS="\
f100_fmt_snprintf_trunc.c F-041 \
f100_fmt_snprintf_int.c F-042 \
f100_fmt_snprintf_float.c F-043 \
f100_fmt_vsnprintf.c F-044 \
f100_fmt_sprintf.c F-045 \
f100_fmt_printf.c F-046 \
f100_fmt_fprintf.c F-047 \
f100_fmt_checked.c F-048 \
f100_fmt_sscanf.c F-049 \
f100_fmt_contract.c F-050 \
f100_io_open_close.c F-051 \
f100_io_readwrite.c F-052 \
f100_io_readline.c F-053 \
f100_io_token.c F-054 \
f100_io_strtol.c F-055 \
f100_io_strtoll.c F-056 \
f100_io_strtod.c F-057 \
f100_io_atoi.c F-058 \
f100_io_fscanf.c F-059 \
f100_io_roundtrip.c F-060"

set -- $CONFIGS
while [ "$#" -ge 2 ]; do
  src=$1; id=$2; shift 2
  name=$(basename "$src" .c)
  bin="$WORK/$name"
  if "$CC" -O1 -g -std=c99 -Wall -Wextra -Wno-format-nonliteral -Wno-format-security -Wno-unused-function $SAN $INC \
       "tests/crt/fuzz/$src" $LIBS -lm -pthread -o "$bin" >"$WORK/$name.build.log" 2>&1; then
    if timeout 30 "$bin" >"$WORK/$name.run.log" 2>&1; then
      printf 'ok   %s %s: %s\n' "$id" "$name" "$(grep '^CONFIG ' "$WORK/$name.run.log" | tail -1)"
      pass=$((pass + 1))
    else
      printf 'FAIL %s %s (run rc=%s)\n' "$id" "$name" "$?"
      tail -20 "$WORK/$name.run.log"
      fail=$((fail + 1))
    fi
  else
    printf 'FAIL %s %s (build)\n' "$id" "$name"
    tail -20 "$WORK/$name.build.log"
    fail=$((fail + 1))
  fi
done

printf 'F-041..F-060: %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
