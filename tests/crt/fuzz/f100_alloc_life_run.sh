#!/bin/sh
# Build and run every deterministic allocation/lifecycle fuzz configuration
# F-021..F-040.  Each config is built with /usr/bin/clang + UBSan against the
# checked-in archives and executed under `timeout 30`.
#
# Exit statuses per config: 0 = PASS, 2 = real DEFECT detected (distinct from
# a harness failure), anything else = FAIL.  This script exits nonzero if any
# config FAILs or reports a DEFECT.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT" || exit 1

CC=/usr/bin/clang
SAFE_LIB=build/safe/libcplus_safecrt.a
LIBC_LIB=build/libc/libcplus_hardened.a
COMMON="-O1 -g -fsanitize=undefined -fno-sanitize-recover=all -std=c11"
SAFE_INC="-Istdlib/safe -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
LIBC_INC="-Ilibc/install-include"

for lib in "$SAFE_LIB" "$LIBC_LIB"; do
  if [ ! -f "$lib" ]; then
    echo "MISSING ARCHIVE: $lib"
    exit 1
  fi
done

TMPD=$(mktemp -d "${TMPDIR:-/tmp}/f100_alloc_life.XXXXXX") || exit 1
trap 'rm -rf "$TMPD"' EXIT HUP INT TERM

passes=0
defects=0
fails=0

run_one() {
  id=$1
  src=$2
  fam=$3
  bin="$TMPD/$id"
  blog="$TMPD/$id.build.log"
  rlog="$TMPD/$id.run.log"

  case "$fam" in
    safe) cmd="$CC $COMMON $SAFE_INC tests/crt/fuzz/$src $SAFE_LIB -pthread -o $bin" ;;
    libc) cmd="$CC $COMMON $LIBC_INC tests/crt/fuzz/$src $LIBC_LIB -lm -pthread -o $bin" ;;
    mixed) cmd="$CC $COMMON $SAFE_INC $LIBC_INC tests/crt/fuzz/$src $SAFE_LIB $LIBC_LIB -lm -pthread -o $bin" ;;
    *) echo "RESULT $id UNKNOWN_FAMILY"; fails=$((fails+1)); return ;;
  esac

  printf 'BUILD_CMD[%s]: %s\n' "$id" "$cmd"
  /bin/sh -c "$cmd" >"$blog" 2>&1
  bs=$?
  if [ "$bs" -ne 0 ]; then
    printf 'RESULT %s BUILD_FAIL(%d)\n' "$id" "$bs"
    sed -n '1,20p' "$blog"
    fails=$((fails+1))
    return
  fi

  printf 'RUN_CMD[%s]: timeout 30 %s\n' "$id" "$bin"
  timeout 30 "$bin" >"$rlog" 2>&1
  rs=$?
  sed -n '1,40p' "$rlog"
  case "$rs" in
    0)
      printf 'RESULT %s PASS\n' "$id"
      passes=$((passes+1))
      ;;
    2)
      printf 'RESULT %s DEFECT\n' "$id"
      defects=$((defects+1))
      ;;
    *)
      printf 'RESULT %s FAIL(%d)\n' "$id" "$rs"
      fails=$((fails+1))
      ;;
  esac
}

while IFS='|' read -r id src fam; do
  case "$id" in ''|\#*) continue ;; esac
  run_one "$id" "$src" "$fam"
done <<'EOF'
F-021|f100_alloc_f021_malloc.c|safe
F-022|f100_alloc_f022_calloc.c|safe
F-023|f100_alloc_f023_realloc.c|safe
F-024|f100_alloc_f024_free.c|safe
F-025|f100_alloc_f025_aligned.c|safe
F-026|f100_alloc_f026_sidetable.c|safe
F-027|f100_alloc_f027_transfer.c|safe
F-028|f100_alloc_f028_injection.c|safe
F-029|f100_alloc_f029_canary.c|safe
F-030|f100_life_f030_cplus_malloc.c|libc
F-031|f100_life_f031_cplus_calloc.c|libc
F-032|f100_life_f032_cplus_realloc.c|libc
F-033|f100_life_f033_cplus_free.c|libc
F-034|f100_life_f034_cplus_aligned.c|libc
F-035|f100_life_f035_cplus_meta.c|libc
F-036|f100_life_f036_cplus_transfer.c|libc
F-037|f100_life_f037_cplus_injection.c|libc
F-038|f100_life_f038_repeated_drop.c|mixed
F-039|f100_life_f039_canary.c|libc
F-040|f100_life_f040_custom_alloc_skip.c|mixed
EOF

printf 'SUMMARY F100 passes=%d defects=%d fails=%d\n' "$passes" "$defects" "$fails"
[ "$fails" -eq 0 ] && [ "$defects" -eq 0 ]
