#!/bin/sh
# Focused regression for issue #235 part 1: STRICT Safe mode must not let a
# Clang long-form option ride the ordinary `-l<lib>` pass-through allow-list.
# normalizeFlag() used to rewrite `--ld-path=...` / `--language=...` into
# `-ld-path=...` / `-language=...`, which start with `-l` and were accepted
# silently even though they change the linker or the backend's parse mode.
# These flags must be rejected by the driver gate before any backend runs,
# while ordinary library and pass-through flags keep working.
#
# Usage: CPLUS_BIN=/path/to/c+ sh tests/compiler/agent1/regression_issue235.sh
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
WORK=${TMPDIR:-/tmp}/cplus-issue235-$$
mkdir -p "$WORK" || exit 1
trap 'rm -rf "$WORK"' EXIT INT TERM

fail() { echo "FAIL: $*" >&2; exit 1; }
ok() { printf '  ok   %s\n' "$1"; }
ENV="env -u SDKROOT -u CPLUS_CLANG PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin"

printf 'int main(void){return 0;}\n' > "$WORK/probe.c"

# reject_gate DESC EXPECT ARGS... -- expect a driver gate diagnostic (not a
# backend error) and a non-zero exit status.
reject_gate() {
  desc=$1; want=$2; shift 2
  if $ENV "$CPLUS" check --strict "$WORK/probe.c" "$@" >"$WORK/log" 2>&1; then
    cat "$WORK/log" >&2
    fail "STRICT accepted $desc"
  fi
  grep -qF "$want" "$WORK/log" || {
    cat "$WORK/log" >&2
    fail "STRICT rejected $desc without '$want'"
  }
  # A backend error would mean the flag reached clang and was reinterpreted
  # rather than rejected by the driver's own gate.
  if grep -q '^clang: error' "$WORK/log"; then
    cat "$WORK/log" >&2
    fail "$desc reached the backend instead of the STRICT gate"
  fi
  ok "rejected $desc before the backend"
}

accept() {
  desc=$1; shift
  if ! $ENV "$CPLUS" check --strict "$WORK/probe.c" "$@" >"$WORK/log" 2>&1; then
    cat "$WORK/log" >&2
    fail "STRICT rejected safe $desc"
  fi
  ok "accepted $desc"
}

# --- the two confirmed bypasses (both joined and two-token spellings) -------
reject_gate "--ld-path=/usr/bin/ld" \
    "pass-through flags change the linker used by the build" \
    --ld-path=/usr/bin/ld
reject_gate "--ld-path /usr/bin/ld" \
    "pass-through flags change the linker used by the build" \
    --ld-path /usr/bin/ld
reject_gate "--language=c++" \
    "pass-through flags change how the C+ safety AST parses" --language=c++
reject_gate "--language=c" \
    "pass-through flags change how the C+ safety AST parses" --language=c
reject_gate "--language c++" \
    "pass-through flags change how the C+ safety AST parses" --language c++

# --- ordinary -l<lib> and valid pass-through flags still work --------------
accept "-lm (library flag)" -lm
accept "-L/usr/lib (library search path)" -L/usr/lib
accept "-I$WORK (include path)" -I"$WORK"
accept "-DFOO=1 (macro)" -DFOO=1
accept "-std=c11 (language standard)" -std=c11
accept "-O2 (safe optimization)" -O2
accept "-Wall (additive warning)" -Wall
accept "-pthread" -pthread
# Explicitly whitelisted long-form spelling: SDK selection, not parse state.
accept "--sysroot=/ (explicit long-form pass-through)" --sysroot=/

if ! $ENV "$CPLUS" build --strict --sanitize=none "$WORK/probe.c" -lm \
    -o "$WORK/probe.bin" >"$WORK/log" 2>&1; then
  cat "$WORK/log" >&2
  fail "STRICT build with an ordinary -lm flag failed"
fi
"$WORK/probe.bin" || fail "built program did not run"
ok "STRICT build links an ordinary -lm library flag"

echo "issue #235 long-form pass-through regression: PASS"
