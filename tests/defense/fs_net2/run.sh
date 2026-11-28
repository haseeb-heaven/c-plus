#!/bin/sh
# Blue-team Agent H -- filesystem / network / http defense regressions.
#
# Builds the deterministic offline harness twice:
#   BEFORE: c+ runs with cwd = out/baseline_root (a copy of stdlib/*.hp with
#           http.hp replaced by the byte-identical pre-fix snapshot) -> F-H1
#           reproduces.  The C+ compiler lowers headers relative to its cwd,
#           so -I cannot swap them; changing cwd is the non-invasive route.
#   AFTER:  c+ runs from the repo root against the live stdlib/http.hp.
# Both builds use --default --no-werror --sanitize=none -lcurl.  The strict checker has two
# known false positives in http.hp (CPLUS-ARITH `SIZE_MAX / size` behind the
# `size != 0 &&` short-circuit, and CPLUS-FFI `buf[0]` on a caller buffer whose
# extent cannot be proven); --default reports them as warnings.  No test is
# weakened and no compiler/runtime file is edited.
#
# Exits 0 only when AFTER is clean AND BEFORE still reproduces the finding.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
HERE="$ROOT/tests/defense/fs_net2"
OUT="$HERE/out"
CPLUS=${CPLUS_CPLUS:-$ROOT/build/c+}
CFLAGS="--default --no-werror --sanitize=none -lcurl"
mkdir -p "$OUT/baseline_root/stdlib"

# Materialise the pre-fix header tree (kept entirely under out/, gitignored).
cp "$ROOT"/stdlib/*.hp "$OUT/baseline_root/stdlib/"
cp "$HERE/baseline/stdlib/http.hp" "$OUT/baseline_root/stdlib/http.hp"

echo "[fs_net2] build BEFORE (pre-fix http.hp, baseline cwd)"
( cd "$OUT/baseline_root" && timeout 180 "$CPLUS" build \
    "$HERE/fs_net2_hardening.cp" $CFLAGS -I. \
    -o "$OUT/harness_before" ) >/dev/null 2>&1

echo "[fs_net2] build AFTER (live stdlib/http.hp)"
( cd "$ROOT" && timeout 180 "$CPLUS" build \
    "$HERE/fs_net2_hardening.cp" $CFLAGS -I"$ROOT" \
    -o "$OUT/harness_after" ) >/dev/null 2>&1

set +e
timeout 60 "$OUT/harness_before" "$OUT/before.log" >"$OUT/before.stdout" 2>&1
BEFORE_RC=$?
timeout 60 "$OUT/harness_after" "$OUT/after.log" >"$OUT/after.stdout" 2>&1
AFTER_RC=$?
set -e

echo "BEFORE $(cat "$OUT/before.stdout") (exit $BEFORE_RC)"
echo "AFTER  $(cat "$OUT/after.stdout") (exit $AFTER_RC)"

# ABI fingerprint: exported symbol sets of the two harness binaries must match.
nm -gU "$OUT/harness_before" | awk '{print $2, $3}' | sort >"$OUT/syms_before.txt"
nm -gU "$OUT/harness_after"  | awk '{print $2, $3}' | sort >"$OUT/syms_after.txt"
if diff -u "$OUT/syms_before.txt" "$OUT/syms_after.txt" >"$OUT/syms.diff" 2>&1; then
  echo "ABI_SYMBOLS identical"
else
  echo "ABI_SYMBOLS CHANGED"
  cat "$OUT/syms.diff"
  exit 1
fi

if [ "$AFTER_RC" -ne 0 ]; then
  echo "FAIL: AFTER harness is not clean"
  exit 1
fi
BEFORE_FAIL=$(awk '$1=="FS_NET2"{print $4}' "$OUT/before.stdout")
if [ "$BEFORE_FAIL" -le 0 ]; then
  echo "FAIL: BEFORE harness did not reproduce finding F-H1"
  exit 1
fi
echo "PASS fs_net2: F-H1 reproduced before ($BEFORE_FAIL failures), fixed after (0)"
exit 0
