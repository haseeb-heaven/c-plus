#!/bin/sh
# Deterministic build-and-run harness for the five file_tools CLI utilities.
#
#   sh apps/legacy_examples/utilities/file_tools/run.sh
#
# All compiler and program invocations are bounded by `timeout 30`.  Fixtures
# are regenerated with fixed bytes by fixtures/generate.sh into a scratch
# directory (.run/fixtures) derived from this script's location: no clock,
# network, interactive prompt or user path is involved.  A scenario passes
# only when the exit code matches, stderr is empty and stdout matches the
# expected bytes exactly (`cmp`).
#
# Final line: UTIL_FILE <tools> <passed> <failed> <refused>
#   tools   = 5 utilities
#   passed  = all scenarios that passed (normal + hostile)
#   failed  = scenarios that failed (plus one per tool that failed to build)
#   refused = hostile scenarios that were correctly REFUSED (subset of passed)
set -u

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$DIR/../../../.." && pwd) || exit 2
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$DIR/.run"
FIX="$OUT/fixtures"
KILL_AFTER=30

PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
export PATH

mkdir -p "$OUT" || exit 2
if ! command -v timeout >/dev/null 2>&1; then
  echo "error: 'timeout' utility not found; refusing unbounded tests" >&2
  exit 2
fi
if [ ! -x "$CPLUS" ]; then
  echo "error: C+ compiler missing: $CPLUS" >&2
  exit 2
fi

cd "$ROOT" || exit 2

# ---- deterministic fixtures -----------------------------------------------
if ! sh "$DIR/fixtures/generate.sh" "$FIX"; then
  echo "error: fixture generation failed" >&2
  exit 2
fi
LONGREL=$(i=0; while [ "$i" -lt 129 ]; do printf 'x'; i=$((i + 1)); done)

tools=5
passed=0
failed=0
refused=0
last_ok=0

build_tool() {
  name=$1
  timeout -k 5 "$KILL_AFTER" "$CPLUS" build "$DIR/$name.cp" \
    --strict --sanitize=none -I. -o "$OUT/$name" \
    >"$OUT/$name.build.log" 2>&1
  brc=$?
  if [ "$brc" -ne 0 ]; then
    echo "BUILD-FAIL $name (rc=$brc)"
    tail -n 10 "$OUT/$name.build.log" | sed 's/^/           /'
    return 1
  fi
  echo "build ok   $name"
  return 0
}

verify() {
  label=$1
  want_rc=$2
  want_file=$3
  got_rc=$4
  ok=1
  [ "$got_rc" -eq "$want_rc" ] || ok=0
  if [ -s "$OUT/$label.err" ]; then ok=0; fi
  if ! cmp -s "$OUT/$label.out" "$want_file"; then ok=0; fi
  if [ "$ok" -eq 1 ]; then
    echo "ok         $label"
    passed=$((passed + 1))
    last_ok=1
  else
    echo "FAIL       $label (rc=$got_rc want=$want_rc)"
    diff -u "$want_file" "$OUT/$label.out" 2>/dev/null | head -n 20 \
      | sed 's/^/           /'
    head -n 5 "$OUT/$label.err" | sed 's/^/           stderr: /'
    failed=$((failed + 1))
    last_ok=0
  fi
}

check() {
  label=$1
  want_rc=$2
  want_file=$3
  shift 3
  rm -f "$OUT/$label.out" "$OUT/$label.err"
  timeout -k 5 "$KILL_AFTER" "$@" \
    >"$OUT/$label.out" 2>"$OUT/$label.err" </dev/null
  verify "$label" "$want_rc" "$want_file" "$?"
}

# A hostile scenario: run check(), then count a correct refusal.
refuse() {
  label=$1
  want_rc=$2
  want_file=$3
  shift 3
  check "$label" "$want_rc" "$want_file" "$@"
  if [ "$last_ok" -eq 1 ]; then
    refused=$((refused + 1))
  fi
}

# A pure side-effect check (no process launch), used for byte-exact copy proof.
cmp_check() {
  label=$1
  lhs=$2
  rhs=$3
  if cmp -s "$lhs" "$rhs"; then
    echo "ok         $label"
    passed=$((passed + 1))
    last_ok=1
  else
    echo "FAIL       $label (files differ)"
    failed=$((failed + 1))
    last_ok=0
  fi
}

# ===========================================================================
# fhash
# ===========================================================================
echo "== fhash =="
if build_tool fhash; then
  BH="$OUT/fhash"

  printf 'FHASH f8934173 6\n' >"$OUT/exp_fh_hello"
  check fh_hello 0 "$OUT/exp_fh_hello" "$BH" "$FIX/hello.txt"

  printf 'FHASH 76efddc5 4096\n' >"$OUT/exp_fh_exact"
  check fh_exact 0 "$OUT/exp_fh_exact" "$BH" "$FIX/hash_exact.bin"

  printf 'FHASH error oversized code=101\n' >"$OUT/exp_fh_over"
  refuse fh_oversized 1 "$OUT/exp_fh_over" "$BH" "$FIX/hash_over.bin"

  printf 'FHASH error missing-file code=109\n' >"$OUT/exp_fh_missing"
  refuse fh_missing 1 "$OUT/exp_fh_missing" "$BH" "$FIX/no-such-file"

  printf 'FHASH error bad-path code=109\n' >"$OUT/exp_fh_badpath"
  refuse fh_badpath 1 "$OUT/exp_fh_badpath" "$BH" "$FIX"
else
  failed=$((failed + 1))
fi

# ===========================================================================
# fdiff
# ===========================================================================
echo "== fdiff =="
if build_tool fdiff; then
  BD="$OUT/fdiff"

  printf '%s\n' \
    'FDiff --- a' \
    'FDiff +++ b' \
    'FDiff @@ -3 +3 @@' \
    'FDiff -alpha' \
    'FDiff +ALPHA' \
    'FDiff  beta' \
    'FDiff  gamma' \
    'FDiff changes 2' >"$OUT/exp_fd_change"
  check fd_change 0 "$OUT/exp_fd_change" "$BD" "$FIX/alpha.txt" "$FIX/alpha2.txt"

  printf '%s\n' \
    'FDiff --- a' \
    'FDiff +++ b' \
    'FDiff @@ -3 +3 @@' \
    'FDiff  alpha' \
    'FDiff  beta' \
    'FDiff  gamma' \
    'FDiff changes 0' >"$OUT/exp_fd_same"
  check fd_same 0 "$OUT/exp_fd_same" "$BD" "$FIX/alpha.txt" "$FIX/alpha.txt"

  printf 'FDIFF error file::read_line: line longer than the buffer code=101\n' \
    >"$OUT/exp_fd_longline"
  refuse fd_longline 1 "$OUT/exp_fd_longline" "$BD" \
    "$FIX/longline.txt" "$FIX/alpha.txt"

  printf 'FDIFF error too-many-lines code=101\n' >"$OUT/exp_fd_manylines"
  refuse fd_bigfile 1 "$OUT/exp_fd_manylines" "$BD" \
    "$FIX/manylines.txt" "$FIX/alpha.txt"

  printf 'FDIFF error missing-file code=109\n' >"$OUT/exp_fd_missing"
  refuse fd_missing 1 "$OUT/exp_fd_missing" "$BD" \
    "$FIX/no-such-file" "$FIX/alpha.txt"

  printf 'FDIFF error bad-path code=109\n' >"$OUT/exp_fd_badpath"
  refuse fd_badpath 1 "$OUT/exp_fd_badpath" "$BD" "$FIX" "$FIX/alpha.txt"
else
  failed=$((failed + 1))
fi

# ===========================================================================
# fcat
# ===========================================================================
echo "== fcat =="
if build_tool fcat; then
  BC="$OUT/fcat"

  printf '%s\n' \
    'FCAT 0 alpha' \
    'FCAT 1 beta' \
    'FCAT 2 gamma' \
    'FCAT size 17 lines 3' >"$OUT/exp_fc_text"
  check fc_text 0 "$OUT/exp_fc_text" "$BC" "$FIX/alpha.txt"

  printf 'FCAT size 0 lines 0\n' >"$OUT/exp_fc_empty"
  check fc_empty 0 "$OUT/exp_fc_empty" "$BC" "$FIX/empty.txt"

  printf 'FCAT error file-too-large code=101\n' >"$OUT/exp_fc_over"
  refuse fc_oversized 1 "$OUT/exp_fc_over" "$BC" "$FIX/cat_over.bin"

  printf 'FCAT error file::read_line: line longer than the buffer code=101\n' \
    >"$OUT/exp_fc_longline"
  refuse fc_longline 1 "$OUT/exp_fc_longline" "$BC" "$FIX/longline.txt"

  printf 'FCAT error missing-file code=109\n' >"$OUT/exp_fc_missing"
  refuse fc_missing 1 "$OUT/exp_fc_missing" "$BC" "$FIX/no-such-file"

  printf 'FCAT error bad-path code=109\n' >"$OUT/exp_fc_badpath"
  refuse fc_badpath 1 "$OUT/exp_fc_badpath" "$BC" "$FIX"
else
  failed=$((failed + 1))
fi

# ===========================================================================
# fsize
# ===========================================================================
echo "== fsize =="
if build_tool fsize; then
  BS="$OUT/fsize"

  printf '%s\n' \
    'FSIZE a.bin 10' \
    'FSIZE b.bin 20' \
    'FSIZE sub/c.bin 30' \
    'FSIZE count 3 total 60' >"$OUT/exp_fs_list"
  check fs_list 0 "$OUT/exp_fs_list" "$BS" "$FIX/tree" a.bin b.bin sub/c.bin

  printf 'FSIZE error not-a-directory code=109\n' >"$OUT/exp_fs_baddir"
  refuse fs_baddir 1 "$OUT/exp_fs_baddir" "$BS" "$FIX/alpha.txt" a.bin

  printf 'FSIZE error path-too-long code=101\n' >"$OUT/exp_fs_longpath"
  refuse fs_longpath 1 "$OUT/exp_fs_longpath" "$BS" "$FIX/tree" "$LONGREL"

  printf 'FSIZE error missing-entry code=109\n' >"$OUT/exp_fs_missing"
  refuse fs_missingentry 1 "$OUT/exp_fs_missing" "$BS" "$FIX/tree" nope.bin
else
  failed=$((failed + 1))
fi

# ===========================================================================
# fcopy
# ===========================================================================
echo "== fcopy =="
if build_tool fcopy; then
  BY="$OUT/fcopy"

  printf '%s\n' 'FCOPY copied 17' 'FCOPY verified 17' >"$OUT/exp_fy_normal"
  check fy_normal 0 "$OUT/exp_fy_normal" "$BY" "$FIX/alpha.txt" "$FIX/dst.txt"
  cmp_check fy_byteexact "$FIX/alpha.txt" "$FIX/dst.txt"

  printf '%s\n' 'FCOPY copied 1024' 'FCOPY verified 1024' >"$OUT/exp_fy_exact"
  check fy_exact 0 "$OUT/exp_fy_exact" "$BY" \
    "$FIX/copy_exact.bin" "$FIX/dst_exact.bin"

  printf 'FCOPY error oversized code=101\n' >"$OUT/exp_fy_over"
  refuse fy_oversized 1 "$OUT/exp_fy_over" "$BY" \
    "$FIX/copy_over.bin" "$FIX/dst_over.bin"

  printf 'FCOPY error missing-file code=109\n' >"$OUT/exp_fy_missing"
  refuse fy_missing 1 "$OUT/exp_fy_missing" "$BY" \
    "$FIX/no-such-file" "$FIX/dst.txt"

  printf 'FCOPY error bad-destination code=109\n' >"$OUT/exp_fy_baddst"
  refuse fy_baddst 1 "$OUT/exp_fy_baddst" "$BY" "$FIX/alpha.txt" "$FIX"
else
  failed=$((failed + 1))
fi

printf 'UTIL_FILE %d %d %d %d\n' "$tools" "$passed" "$failed" "$refused"
[ "$failed" -eq 0 ]
