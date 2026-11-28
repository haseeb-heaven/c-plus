#!/bin/sh
# Build and verify three additional self-contained olive.c examples with
# the C+ compiler (CPLUS_BIN override) and clang fallback.
# Prints: OLIVE_EXAMPLES <cases> <pass> <fail>
set -u
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$D" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
OUT="$D/out"
mkdir -p "$OUT"
ROOT=$(CDPATH= cd -- "$D/../../.." && pwd)
C=${CPLUS_BIN:-"$ROOT/build/c+"}
pass=0; fail=0; cases=0
check() {
  cases=$((cases + 1))
  if [ "$1" = "ok" ]; then pass=$((pass + 1));
  else fail=$((fail + 1)); echo "OLIVE_EXAMPLES FAIL $2"; fi
}

for n in lines text squish; do
  BIN="$OUT/${n}_demo"
  PPM="$OUT/${n}.ppm"
  LOG="$OUT/${n}.log"
  rm -f "$BIN" "$PPM" "$LOG"
  built=no
  if [ -x "$C" ]; then
    "$C" build "$D/examples/${n}_demo.c" --legacy-c --sanitize=none -std=c11 -I"$D" -o "$BIN" >"$OUT/${n}.build.log" 2>&1 && built=c+
  fi
  if [ "$built" = no ]; then
    /usr/bin/clang -std=c11 -O2 -Wall -Wextra -Werror -I"$D" "$D/examples/${n}_demo.c" -o "$BIN" >"$OUT/${n}.build.log" 2>&1 && built=clang
  fi
  if [ "$built" = no ]; then
    check no "build-$n"; tail -5 "$OUT/${n}.build.log"; continue
  fi
  check ok "build-$n"
  "$BIN" >"$LOG" 2>&1 || { check no "run-$n"; cat "$LOG"; continue; }
  check ok "run-$n"
  case "$n" in
    lines)  exp=4f4d04e1e2b7b93705190dbf293544fec6f050e1307ab48ef536834b0f653a01 ;;
    text)   exp=ab67abddecceb6379b16c14aa402c016305de90116e6ba12c83f9250c36f1ea3 ;;
    squish) exp=7c520a2b28d812eadb857a5a484a3ca8af734c673e7dd5a1a4c970c71d77c18c ;;
  esac
  sha=$(shasum -a 256 "$PPM" | awk "{print \$1}")
  [ "$sha" = "$exp" ] && check ok "sha-$n" || check no "sha-$n"
  [ "$(wc -c < "$PPM" | tr -d "[:space:]")" = "230415" ] && check ok "bytes-$n" || check no "bytes-$n"
done

echo "OLIVE_EXAMPLES $cases $pass $fail"
[ "$fail" -eq 0 ]
