#!/bin/sh
# Runs every 100-scenario method suite one by one, in both layers where
# supported, under plain and UBSan builds. Missing files are SKIPped while
# parallel generation is still in progress.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CLANG:-/usr/bin/clang}
OUT="$ROOT/build/hundred"
mkdir -p "$OUT"
sh tests/libc/build_libc.sh >/dev/null
sh scripts/build_safecrt.sh >/dev/null

SAFE="$ROOT/build/safe/libcplus_safecrt.a"
LIBC="$ROOT/build/libc/libcplus_hardened.a"
INC="-I. -Istdlib/safe -Ilibc -Ilibc/install-include -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
LIBC_ONLY="-I. -Ilibc -Ilibc/install-include"
pass=0; fail=0; skip=0

run_one() {
  file=$1; name=$2; variant=$3; san=$4
  if [ "$variant" = plain ]; then
    inc="$LIBC_ONLY"; libs="$LIBC"
  else
    inc="$INC"; libs="$SAFE $LIBC"
  fi
  bin="$OUT/${name}_${variant}_${san}"
  if "$CC" -O1 -g -std=c99 -Wall -Wextra -pedantic \
      -fno-builtin-fmax -fno-builtin-fmin -fno-builtin-fmaxf -fno-builtin-fminf \
      -fno-builtin-fmaxl -fno-builtin-fminl \
      $san $inc "$file" $libs -lm -pthread \
      -o "$bin" >"$OUT/${name}_${variant}_${san}.build.log" 2>&1 \
     && timeout 60 "$bin" >"$OUT/${name}_${variant}_${san}.run.log" 2>&1; then
    log="$OUT/${name}_${variant}_${san}.run.log"
    summary=$(grep -E "${name}: [0-9]+/[0-9]+ scenarios ok" "$log" | tail -1)
    counts=$(printf '%s' "$summary" | sed -n 's/.*[^0-9]\([0-9][0-9]*\)\/\([0-9][0-9]*\) scenarios ok.*/\1 \2/p')
    findings=$(grep -c 'FINDING' "$log" 2>/dev/null || true)
    got=${counts%% *}; want=${counts##* }
    if [ -z "$counts" ] || [ "$got" != "$want" ] || [ "${findings:-0}" -ne 0 ]; then
      echo "FAIL $name ($variant/$san): summary='$summary' findings=${findings:-0}"
      grep 'FINDING' "$log" | head -5
      fail=$((fail + 1))
    else
      echo "ok   $name ($variant/$san): $summary"
      pass=$((pass + 1))
    fi
  else
    echo "FAIL $name ($variant/$san)"
    tail -10 "$OUT/${name}_${variant}_${san}.build.log" 2>/dev/null || true
    tail -10 "$OUT/${name}_${variant}_${san}.run.log" 2>/dev/null || true
    fail=$((fail + 1))
  fi
}

for f in tests/methods/hundred_*.c; do
  [ -f "$f" ] || { echo "no hundred_* files yet"; break; }
  name=$(basename "$f" .c)
  case "$name" in
    hundred_conversion|hundred_wchar|hundred_math|hundred_time_fenv|hundred_logging_threads)
      run_one "$f" "$name" libc ""
      run_one "$f" "$name" libc "-fsanitize=undefined -fno-sanitize-recover=all -fno-sanitize=float-divide-by-zero"
      ;;
    *)
      run_one "$f" "$name" safecrt ""
      run_one "$f" "$name" safecrt "-fsanitize=undefined -fno-sanitize-recover=all -fno-sanitize=float-divide-by-zero"
      run_one "$f" "$name" plain ""
      run_one "$f" "$name" plain "-fsanitize=undefined -fno-sanitize-recover=all -fno-sanitize=float-divide-by-zero"
      ;;
  esac
done

echo "hundred-scenario suites: $pass passed, $fail failed, $skip skipped"
[ "$fail" -eq 0 ]
