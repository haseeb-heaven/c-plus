#!/bin/sh
# Bounded runner for the math3d.hp suite.
#
#   sh tests/stdlib/math3d/run.sh
#
# Build and run are each wrapped in `timeout` (30s).  A timeout or any
# non-zero exit is a failure, never a pass.  Acceptance requires:
#   * build exits 0 and run exits 0,
#   * stderr is empty,
#   * stdout matches the embedded expected fixture byte-for-byte,
#   * the final "MATH3D <checks> <failures>" line reports failures == 0,
#   * every "<tag> <1|0>" line reports 1.
#
# Exit status: 0 = verified, 1 = failure, 2 = setup/tool missing.
set -u

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$DIR/../../.." && pwd) || exit 2
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT=${M3_OUT:-"$ROOT/build/math3d"}
BUILD_TIMEOUT=${M3_BUILD_TIMEOUT:-30}
RUN_TIMEOUT=${M3_RUN_TIMEOUT:-30}
KILL_AFTER=${M3_KILL_AFTER:-5}

# Keep `timeout`, the compiler and `diff` deterministic across hosts.
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
export PATH

die() { printf 'error: %s\n' "$*" >&2; exit 2; }

mkdir -p "$OUT" || die "cannot create output directory: $OUT"
command -v timeout >/dev/null 2>&1 || die "'timeout' utility not found; refusing unbounded tests"
command -v diff >/dev/null 2>&1 || die "'diff' utility not found; cannot verify exact output"
[ -x "$CPLUS" ] || die "C+ compiler missing: $CPLUS (build it first; override with CPLUS_BIN)"

SRC="$DIR/test_math3d.cp"
BIN="$OUT/test_math3d"
SO="$OUT/test_math3d.stdout"
SE="$OUT/test_math3d.stderr"
EXPECTED="$OUT/test_math3d.expected"
DIFFOUT="$OUT/test_math3d.diff"
rm -f "$BIN" "$SO" "$SE" "$EXPECTED" "$DIFFOUT"

# Exact expected stdout (36 checks + final summary).
cat > "$EXPECTED" <<'EOF'
t_finite_ok 1
t_finite_nan 1
t_finite_inf 1
t_v_add 1
t_v_sub 1
t_v_scale 1
t_v_dot 1
t_v_cross 1
t_v_length 1
t_v_normalize 1
t_m_identity 1
t_m_multiply_id 1
t_m_translate 1
t_m_scale 1
t_m_rotate 1
t_m_perspective 1
t_m_look_at 1
t_q_mul_identity 1
t_q_normalize 1
t_q_from_axis 1
t_q_roundtrip 1
t_refuse_v_add 1
t_refuse_v_scale_inf 1
t_refuse_v_dot 1
t_refuse_m_multiply 1
t_refuse_q_nonfinite 1
t_refuse_perspective 1
t_zero_v_normalize 1
t_zero_m_rotate_axis 1
t_zero_persp_aspect 1
t_zero_persp_nearfar 1
t_zero_lookat_eye 1
t_zero_lookat_up 1
t_zero_q_axis 1
t_zero_q_normalize 1
t_zero_q_to_mat4 1
MATH3D 36 0
EOF

# ---- build (bounded) -----------------------------------------------------
timeout -k "$KILL_AFTER" "$BUILD_TIMEOUT" "$CPLUS" build "$SRC" \
  --default --sanitize=none -I"$ROOT" -o "$BIN" >"$OUT/build.log" 2>&1
brc=$?
if [ "$brc" -eq 124 ] || [ "$brc" -ge 128 ]; then
  printf 'TIMEOUT build: exceeded %ss (rc=%s)\n' "$BUILD_TIMEOUT" "$brc"
  exit 1
fi
if [ "$brc" -ne 0 ]; then
  printf 'BUILD-FAIL rc=%s\n' "$brc"
  sed 's/^/           /' "$OUT/build.log"
  exit 1
fi

# ---- run (bounded) -------------------------------------------------------
timeout -k "$KILL_AFTER" "$RUN_TIMEOUT" "$BIN" </dev/null >"$SO" 2>"$SE"
rrc=$?
if [ "$rrc" -eq 124 ] || [ "$rrc" -ge 128 ]; then
  printf 'TIMEOUT run: exceeded %ss (rc=%s)\n' "$RUN_TIMEOUT" "$rrc"
  exit 1
fi

why=""
if [ "$rrc" -ne 0 ]; then why="$why exit=$rrc"; fi
if [ -s "$SE" ]; then why="$why stderr"; fi

diff -u "$EXPECTED" "$SO" >"$DIFFOUT" 2>&1
drc=$?
if [ "$drc" -ne 0 ]; then why="$why stdout-mismatch"; fi

final=$(tail -n 1 "$SO")
case "$final" in
  "MATH3D "*) ;;
  *) why="$why bad-summary" ;;
esac
failures=${final##* }
case "$failures" in
  ''|*[!0-9]*) why="$why bad-failure-count" ;;
  *) [ "$failures" -eq 0 ] || why="$why failures=$failures" ;;
esac
bad_tags=$(awk 'NF==2 && $2=="0" {n++} END{print n+0}' "$SO")
[ "$bad_tags" -eq 0 ] || why="$why bad-tags=$bad_tags"

if [ -n "$why" ]; then
  printf 'FAIL math3d:%s\n' "$why"
  [ -s "$DIFFOUT" ] && sed 's/^/           /' "$DIFFOUT"
  [ -s "$SE" ] && sed 's/^/           /' "$SE"
  exit 1
fi

printf 'ok math3d: %s\n' "$final"
printf 'math3d: PASS (build rc=%s, run rc=%s, exact stdout, empty stderr)\n' "$brc" "$rrc"
exit 0
