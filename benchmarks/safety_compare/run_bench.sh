#!/bin/sh
# Speed comparison: plain C, C+ safe, C+ legacy, Rust, the inspiration language.
# Fil-C is NOT measured because no filc toolchain is installed; this is recorded
# as a limitation, not treated as a zero or a pass.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
OUT="$ROOT/build/bench_safety"
mkdir -p "$OUT"
CPLUS=$ROOT/build/c+
CLANG=/usr/bin/clang
RUSTC=${CPLUS_RUSTC:-rustc}
SCOPED=${CPLUS_SCOPED:-scoped}

build_all() {
  w=$1
  "$CLANG" -O2 "benchmarks/safety_compare/$w.c" -o "$OUT/c_$w" || return 1
  PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin "$CPLUS" build --release "benchmarks/safety_compare/$w.c" -o "$OUT/cplus_safe_$w" >"$OUT/cplus_safe_$w.build.log" 2>&1 || return 1
  PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin "$CPLUS" build --release --legacy-c "benchmarks/safety_compare/$w.c" -o "$OUT/cplus_legacy_$w" >"$OUT/cplus_legacy_$w.build.log" 2>&1 || return 1
  "$RUSTC" -O "benchmarks/safety_compare/$w.rs" -o "$OUT/rust_$w" || return 1
  "$SCOPED" build "benchmarks/safety_compare/$w.scoped" -o "$OUT/scoped_$w" >"$OUT/scoped_$w.build.log" 2>&1 || return 1
}

for w in loop nested; do
  if ! build_all "$w"; then echo "FAIL build $w"; tail -5 "$OUT"/*"$w".build.log 2>/dev/null; continue; fi
  echo "=== $w ==="
  python3 - "$w" "$OUT" <<'PY'
import subprocess,sys,time,statistics
w,out=sys.argv[1],sys.argv[2]
variants=['c','cplus_safe','cplus_legacy','rust','scoped']
results={}
for v in variants:
    bin=f'{out}/{v}_{w}'
    times=[]
    val=None
    for k in range(5):
        t=time.perf_counter()
        r=subprocess.run([bin],capture_output=True,text=True,timeout=300)
        dt=time.perf_counter()-t
        if k>0: times.append(dt)   # discard warmup
        val=r.stdout.strip()
    results[v]=(statistics.median(times),val)
    print(f'{v:14s} {results[v][0]:8.4f}s  checksum={val}')
base=results['c'][0]
print(f'ratios vs plain C: '+', '.join(f'{v}={results[v][0]/base:.2f}x' for v in variants if v!='c'))
PY
done
echo "note: Fil-C not measured (no filc toolchain installed). C+ safe includes UBSan instrumentation; C+ legacy does not."
