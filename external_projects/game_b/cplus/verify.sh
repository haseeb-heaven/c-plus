#!/bin/bash
# Self-contained verification for the game_b (2048.c) C+ conversion.
# Prints: CONVERT game_b <checks> <matched> <differed>
# Exits 0 iff every check matched, nonzero otherwise.
set -u
ROOT="/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib"
HERE="$ROOT/external_projects/game_b/cplus"
ORIG="$ROOT/external_projects/game_b/original/2048.c"
RES="$HERE/evidence/results"
TDIR="$(mktemp -d /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/game_b_verify.XXXXXX)"
trap 'rm -rf "$TDIR"' EXIT
mkdir -p "$RES"
: > "$RES/summary.txt"

# ---- reference binary (original C, recorded output) ----------------------
if [ -x "$ROOT/external_projects/game_b/2048_build" ]; then
  REF="$ROOT/external_projects/game_b/2048_build"
elif command -v clang >/dev/null 2>&1; then
  SDK="$(xcrun --show-sdk-path 2>/dev/null || true)"
  if [ -n "$SDK" ]; then
    clang -isysroot "$SDK" -O2 -o "$TDIR/2048_ref" "$ORIG" 2>"$RES/reference_build.log"
    REF="$TDIR/2048_ref"
  fi
fi
if [ -z "${REF:-}" ] || [ ! -x "$REF" ]; then
  echo "CONVERT game_b 0 0 0"
  echo "VERIFIED no reference available" >&2
  exit 2
fi

# ---- build the modern C+ binary (exact required command) -----------------
cd "$ROOT"
./build/c+ build external_projects/game_b/cplus/2048_modern.cp \
  --strict --sanitize=none -I. \
  -o external_projects/game_b/cplus/2048_modern \
  > "$RES/build.log" 2>&1
BUILD_RC=$?
echo "build_exit=$BUILD_RC" >> "$RES/summary.txt"
if [ "$BUILD_RC" -ne 0 ]; then
  cat "$RES/build.log"
  echo "CONVERT game_b 0 0 0"
  exit 3
fi
MOD="$HERE/2048_modern"

checks=0; matched=0; differed=0
norm() { sed "s#$REF#./2048#g; s#$MOD#./2048#g" "$1" > "$2"; }

record() {
  local name="$1" rc1="$2" rc2="$3"
  checks=$((checks+1))
  if cmp -s "$TDIR/ro.txt" "$TDIR/mo.txt" && [ "$rc1" = "$rc2" ] && cmp -s "$TDIR/re.txt" "$TDIR/me.txt"; then
    matched=$((matched+1))
    echo "MATCH $name exit=$rc1" >> "$RES/summary.txt"
    cp "$TDIR/mo.txt" "$RES/$name.stdout"
    printf '%s\n' "$rc1" > "$RES/$name.exit"
  else
    differed=$((differed+1))
    {
      echo "DIFFER $name ref_exit=$rc1 mod_exit=$rc2"
      diff "$TDIR/ro.txt" "$TDIR/mo.txt" | head -40
    } >> "$RES/summary.txt"
    cp "$TDIR/ro.txt" "$RES/$name.ref.stdout"
    cp "$TDIR/mo.txt" "$RES/$name.mod.stdout"
  fi
}

# ---- Phase A: deterministic CLI (no board, no RNG) -----------------------
for arg in --help -h --version -v test --bogus -x; do
  timeout 30 "$REF" "$arg" > "$TDIR/r0.txt" 2> "$TDIR/re.txt"; rc1=$?
  timeout 30 "$MOD" "$arg" > "$TDIR/m0.txt" 2> "$TDIR/me.txt"; rc2=$?
  norm "$TDIR/r0.txt" "$TDIR/ro.txt"
  norm "$TDIR/m0.txt" "$TDIR/mo.txt"
  record "cli_${arg#--}" "$rc1" "$rc2"
done

# ---- Phase B: board/RNG modes (launch concurrently -> same time seed) ----
run_board_case() {
  local name="$1" input="$2"; shift 2
  printf '%s' "$input" | timeout 30 "$REF" "$@" > "$TDIR/r0.txt" 2> "$TDIR/re.txt" & p1=$!
  printf '%s' "$input" | timeout 30 "$MOD" "$@" > "$TDIR/m0.txt" 2> "$TDIR/me.txt" & p2=$!
  wait $p1; rc1=$?
  wait $p2; rc2=$?
  norm "$TDIR/r0.txt" "$TDIR/ro.txt"
  norm "$TDIR/m0.txt" "$TDIR/mo.txt"
  record "$name" "$rc1" "$rc2"
}
run_board_case mode_default_xqy 'xqy'
run_board_case mode_default_move 'aaaaaassssddddwwww'
run_board_case mode_blackwhite 'aaaaaassss' blackwhite
run_board_case mode_bluered 'aaaaaassss' bluered
run_board_case mode_whiteblack 'aaaaaassss' whiteblack
run_board_case mode_restart 'ryy'

# ---- Phase C: SIGINT handler (concurrent, same time seed) ----------------
( sleep 5 | timeout 10 "$REF" > "$TDIR/r0.txt" 2> "$TDIR/re.txt" ) & w1=$!
( sleep 5 | timeout 10 "$MOD" > "$TDIR/m0.txt" 2> "$TDIR/me.txt" ) & w2=$!
sleep 0.6
pkill -INT -f 'timeout 10 .*2048' 2>/dev/null || true
wait $w1 2>/dev/null; rc1=$?
wait $w2 2>/dev/null; rc2=$?
norm "$TDIR/r0.txt" "$TDIR/ro.txt"
norm "$TDIR/m0.txt" "$TDIR/mo.txt"
record "sigint" "$rc1" "$rc2"

echo "CONVERT game_b $checks $matched $differed"
echo "CONVERT game_b $checks $matched $differed" >> "$RES/summary.txt"
[ "$differed" -eq 0 ]
