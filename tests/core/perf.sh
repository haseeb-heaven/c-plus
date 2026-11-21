#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tests/out/core/perf"
mkdir -p "$OUT"
SRC="$ROOT/tests/cases/hello.c"
echo "== compile time + peak memory =="
for cmd in "c+ safe" "c+ legacy" "clang O2"; do
  case "$cmd" in
    "c+ safe")
      /usr/bin/time -l "$ROOT/build/c+" check "$SRC" -o /dev/null \
        >"$OUT/$cmd.out" 2>"$OUT/$cmd.time" || true ;;
    "c+ legacy")
      /usr/bin/time -l "$ROOT/build/c+" check "$SRC" --legacy-c -o /dev/null \
        >"$OUT/$cmd.out" 2>"$OUT/$cmd.time" || true ;;
    "clang O2")
      /usr/bin/time -l /usr/bin/clang -std=gnu17 -O2 -fsyntax-only "$SRC" \
        >"$OUT/$cmd.out" 2>"$OUT/$cmd.time" || true ;;
  esac
  printf '%-12s %s %s\n' "$cmd" "$(grep real "$OUT/$cmd.time" | tr -s ' ')" \
    "$(grep 'maximum resident set size' "$OUT/$cmd.time" | tr -s ' ')"
done
echo "== executable size =="
"$ROOT/build/c+" build "$SRC" -o "$OUT/cplus_safe" >/dev/null 2>&1
"$ROOT/build/c+" build "$SRC" --legacy-c -o "$OUT/cplus_legacy" >/dev/null 2>&1
/usr/bin/clang -std=gnu17 -O2 "$SRC" -o "$OUT/clang_o2" >/dev/null 2>&1
wc -c "$OUT/cplus_safe" "$OUT/cplus_legacy" "$OUT/clang_o2"
