#!/bin/sh
# Fair performance comparison: the C+ containers against the hand-written C
# equivalent, doing identical work.
#
#   safe   = -O0 -g + the sanitizer set safe mode mandates
#   fast   = -O2 (C+ still carries the sanitizers; plain C does not)
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$ROOT/build/bench"
CC=${CPLUS_CLANG:-clang}
SAFE="-O0 -g -fsanitize=undefined,bounds,null,alignment -fno-sanitize-recover=all -fno-omit-frame-pointer"

mkdir -p "$OUT"
cd "$ROOT" || exit 1

$CC $SAFE tests/stdlib/bench_plain.c -o "$OUT/plain_safe" || exit 1
"$CPLUS" build tests/stdlib/bench_compare.cp -o "$OUT/cplus_safe" >/dev/null || exit 1
$CC -O2 tests/stdlib/bench_plain.c -o "$OUT/plain_fast" || exit 1
"$CPLUS" build --release tests/stdlib/bench_compare.cp -o "$OUT/cplus_fast" >/dev/null || exit 1

run3extra() { # name -> best-of-three "json_ms file_ms alloc_ms"
  name="$1"
  f="$OUT/$name.runs"
  : >"$f"
  i=0
  while [ $i -lt 3 ]; do
    "$OUT/$name" >>"$f"
    i=$((i + 1))
  done
  awk 'NR==1 {a=$1; b=$2; c=$3} {if ($1<a) a=$1; if ($2<b) b=$2; if ($3<c) c=$3}
       END {printf "%s %s %s\n", a, b, c}' "$f"
}

run3() { # name -> best-of-three "vector_ms string_ms checksum"
  name="$1"
  f="$OUT/$name.runs"
  : >"$f"
  i=0
  while [ $i -lt 3 ]; do
    "$OUT/$name" >>"$f"
    i=$((i + 1))
  done
  awk 'NR==1 {v=$1; s=$2} {if ($1<v) v=$1; if ($2<s) s=$2; sum=$3}
       END {printf "%s %s %s\n", v, s, sum}' "$f"
}

# extra: JSON parse, file I/O, allocation throughput (same work both sides)
$CC $SAFE tests/stdlib/bench_extra.c -o "$OUT/extra_plain_safe" -Ithird_party/yyjson \
  -Lbuild/ext -lyyjson || exit 1
"$CPLUS" build tests/stdlib/bench_extra.cp -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o "$OUT/extra_cplus_safe" >/dev/null || exit 1

printf '%-14s %10s %10s\n' "workload" "vector_ms" "string_ms"
printf '%-14s %10s %10s\n' "--------------" "---------" "---------"
for b in plain_safe cplus_safe plain_fast cplus_fast; do
  set -- $(run3 "$b")
  printf '%-14s %10s %10s\n' "$b" "$1" "$2"
done
printf '\n%-14s %10s %10s %10s\n' "extra" "json_ms" "file_ms" "alloc_ms"
printf '%-14s %10s %10s %10s\n' "--------------" "-------" "-------" "--------"
for b in extra_plain_safe extra_cplus_safe; do
  set -- $(run3extra "$b")
  printf '%-14s %10s %10s %10s\n' "$b" "$1" "$2" "$3"
done

echo
echo "checksum must match across every row (no work skipped):"
for b in plain_safe cplus_safe plain_fast cplus_fast; do
  printf '%-14s %s\n' "$b" "$("$OUT/$b" | awk '{print $3}')"
done
for b in extra_plain_safe extra_cplus_safe; do
  printf '%-14s %s\n' "$b" "$("$OUT/$b" | awk '{print $4}')"
done
