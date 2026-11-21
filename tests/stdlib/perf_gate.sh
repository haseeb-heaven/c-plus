#!/bin/sh
# Performance regression gate (issue #8): the C+ wrappers may not exceed these
# ratios against the equivalent hand-written C in safe mode.  Thresholds are
# deliberately generous because this runs on shared machines.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
CC=${CPLUS_CLANG:-clang}
OUT="$ROOT/build/perf"
SAFE="-O0 -g -fsanitize=undefined,bounds,null,alignment -fno-sanitize-recover=all -fno-omit-frame-pointer"
mkdir -p "$OUT"
cd "$ROOT" || exit 1

# threshold per workload (C+ / plain C)
T_VECTOR=8.0
T_STRING=8.0
T_JSON=3.0
T_FILE=3.0
T_ALLOC=6.0

$CC $SAFE tests/stdlib/bench_plain.c -o "$OUT/plain" || exit 1
"$CPLUS" build tests/stdlib/bench_compare.cp -o "$OUT/cplus" >/dev/null || exit 1
$CC $SAFE tests/stdlib/bench_extra.c -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o "$OUT/extra_plain" >/dev/null 2>&1 || exit 1
"$CPLUS" build tests/stdlib/bench_extra.cp -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o "$OUT/extra_cplus" >/dev/null || exit 1

ratio() { # a b -> a/b rounded
  awk -v a="$1" -v b="$2" 'BEGIN { if (b <= 0) print 0; else printf "%.2f", a/b }'
}

fail=0
check() { # name value threshold
  awk -v n="$1" -v v="$2" -v t="$3" 'BEGIN { exit !(v <= t) }' || {
    echo "FAIL $1: ratio $2 exceeds $3" >&2
    fail=1
  }
}

plain=$("$OUT/plain" | awk '{print $1" "$2}')
cplus=$("$OUT/cplus" | awk '{print $1" "$2}')
set -- $plain; pv=$1; ps=$2
set -- $cplus; cv=$1; cs=$2
rv=$(ratio "$cv" "$pv")
rs=$(ratio "$cs" "$ps")
check vector "$rv" "$T_VECTOR"
check string "$rs" "$T_STRING"

plain=$("$OUT/extra_plain" | awk '{print $1" "$2" "$3}')
cplus=$("$OUT/extra_cplus" | awk '{print $1" "$2" "$3}')
set -- $plain; pj=$1; pf=$2; pa=$3
set -- $cplus; cj=$1; cf=$2; ca=$3
rj=$(ratio "$cj" "$pj")
rf=$(ratio "$cf" "$pf")
ra=$(ratio "$ca" "$pa")
check json "$rj" "$T_JSON"
check file "$rf" "$T_FILE"
check alloc "$ra" "$T_ALLOC"

printf 'perf ratios (C+ / plain C, lower is better): vector %s string %s json %s file %s alloc %s\n' \
  "$rv" "$rs" "$rj" "$rf" "$ra"
if [ "$fail" -ne 0 ]; then
  echo "performance regression: one or more ratios exceed the gate" >&2
  exit 1
fi
echo "performance gate: ok"
