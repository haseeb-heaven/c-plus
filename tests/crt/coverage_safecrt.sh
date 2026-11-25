#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CLANG:-clang}
COV=${LLVM_COV:-llvm-cov}
PROFDATA=${LLVM_PROFDATA:-llvm-profdata}
if command -v xcrun >/dev/null 2>&1; then
  found=$({ xcrun --find llvm-cov; } 2>/dev/null || true)
  [ -n "$found" ] && COV="$found"
  found=$({ xcrun --find llvm-profdata; } 2>/dev/null || true)
  [ -n "$found" ] && PROFDATA="$found"
fi

sh "$ROOT/scripts/build_safecrt.sh" >/dev/null
sh "$ROOT/tests/libc/build_libc.sh" >/dev/null

OUT="$ROOT/build/safecrt-cov"
rm -rf "$OUT"
mkdir -p "$OUT"
CFLAGS_W="-O0 -g -fno-omit-frame-pointer -Wall -Wextra -pedantic -std=c99 -fprofile-instr-generate -fcoverage-mapping -Ithird_party/safeclib/include -Istdlib/safe -Ithird_party/exceptions4c"
WOBJS=""
FIRST_OBJ=""
OBJ_ARGS=""
for f in "$ROOT"/stdlib/safe/src/*.c; do
  o="$OUT/w_$(basename "$f" .c).o"
  "$CC" $CFLAGS_W -c "$f" -o "$o"
  WOBJS="$WOBJS $o"
  if [ -z "$FIRST_OBJ" ]; then
    FIRST_OBJ="$o"
  else
    OBJ_ARGS="$OBJ_ARGS -object $o"
  fi
done
ar rcs "$OUT/libsafecrt_cov.a" "$ROOT"/build/safe/sc_*.o "$ROOT"/build/safe/up_*.o "$ROOT"/build/safe/v_*.o "$ROOT"/build/safe/e4c_*.o $WOBJS

pass=0; fail=0
for t in "$ROOT"/tests/crt/unit/test_*.c "$ROOT"/tests/crt/test_safe_*.c \
         "$ROOT"/tests/exceptions/test_*.c "$ROOT"/tests/runtime/test_runtime_cleanup.c; do
  [ -e "$t" ] || continue
  base=$(basename "$t" .c)
  tag=$(echo "$t" | sed "s|$ROOT/||; s|/|_|g; s|\.c$||")
  b="$OUT/$tag"
  EXTRA=""
  EXTRA_INC=""
  LINK_OBJS="$OUT/libsafecrt_cov.a"
  case "$t" in
    *test_runtime_cleanup.c)
      EXTRA="-fsanitize=undefined,bounds,null,alignment -fsanitize-recover=all -fno-sanitize-trap=all -fno-omit-frame-pointer"
      LINK_OBJS="$OUT/w_safe_runtime.o $OUT/w_safe_log.o $OUT/w_safe_stats.o"
      ;;
    *safe_c_api*|*audit_high*|*p0_safe_crt*|*p0_alloc_contract*|*safe_c_mapping*|*checked_runtime_safety*)
      EXTRA_INC="-I$ROOT/libc -I$ROOT/libc/install-include"
      LINK_OBJS="$OUT/libsafecrt_cov.a $ROOT/build/libc/libcplus_hardened.a"
      ;;
  esac
  if "$CC" -O0 -std=c99 $EXTRA -fprofile-instr-generate -I"$ROOT" -I"$ROOT/stdlib/safe" \
      -I"$ROOT/third_party/safeclib/include" -I"$ROOT/third_party/intel-safestringlib/include" \
      -I"$ROOT/third_party/exceptions4c" $EXTRA_INC "$t" $LINK_OBJS -lm -lpthread \
      -o "$b" >"$OUT/$tag.build.log" 2>&1; then
    if LLVM_PROFILE_FILE="$OUT/$tag.profraw" "$b" >"$OUT/$tag.out" 2>&1; then
      pass=$((pass + 1))
    else
      fail=$((fail + 1))
      echo "FAIL $tag"
    fi
  else
    fail=$((fail + 1))
    echo "FAIL BUILD $tag"
  fi
done

"$PROFDATA" merge -sparse "$OUT"/*.profraw -o "$OUT/safe.profdata"
REPORT="$OUT/report.txt"
"$COV" report "$FIRST_OBJ" $OBJ_ARGS -instr-profile="$OUT/safe.profdata" >"$REPORT"
cat "$REPORT"
echo "safecrt coverage tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
python3 - "$REPORT" <<'PY'
import sys
lines = open(sys.argv[1]).read().splitlines()
total_line = [l for l in lines if l.startswith("TOTAL")]
if not total_line:
    sys.exit("missing TOTAL line")
parts = total_line[0].split()
line_cov = float(parts[9].rstrip("%"))
print(f"safecrt total line coverage: {line_cov:.2f}%")
if line_cov < 80.0:
    sys.exit(f"safecrt line coverage {line_cov:.2f}% < 80.0%")
PY
