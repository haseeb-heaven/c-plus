#!/bin/sh
# Deterministic runner for the scene-graph conformance suite.
#
# Usage (from the repository root):
#   sh tests/stdlib/scene3d/run.sh
#
# Builds tests/stdlib/scene3d/test_scene3d.cp with the project's default
# mode / no sanitizer / -I. and the hardened libc archive, then runs it with
# an empty stdin under a timeout.  A run is accepted only when:
#   * build and run both exit 0,
#   * the program's stderr is empty,
#   * the last stdout line is exactly "SCENE3D <checks> 0",
#   * every "<tag> <0|1>" line printed 1 and their count equals <checks>,
#   * stdout is byte-identical to the expected transcript below (this pins
#     the preorder traversal, every refusal, the allocation-failure points
#     and the canary probes to exact values),
#   * no "<tag> 0" line was printed.
#
# Exit status: 0 = verified; 1 = a check/run failed; 2 = tool missing.
set -u

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$DIR/../../.." && pwd) || exit 2
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT=${S3_OUT:-"$DIR/.run"}
BUILD_TIMEOUT=${S3_BUILD_TIMEOUT:-60}
RUN_TIMEOUT=${S3_RUN_TIMEOUT:-30}
KILL_AFTER=${S3_KILL_AFTER:-5}

PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
export PATH

mkdir -p "$OUT" || exit 2
if ! command -v timeout >/dev/null 2>&1; then
  echo "error: 'timeout' utility not found; refusing unbounded tests" >&2
  exit 2
fi
if [ ! -x "$CPLUS" ]; then
  echo "error: C+ compiler missing: $CPLUS (build it first)" >&2
  exit 2
fi

BIN="$OUT/test_scene3d"
SO="$OUT/stdout.txt"
SE="$OUT/stderr.txt"
BLOG="$OUT/build.log"
EXPECTED="$OUT/expected.txt"
rm -f "$BIN" "$SO" "$SE" "$EXPECTED"

cat >"$EXPECTED" <<'EOF'
a_init 1
a_root 1
a_root_name 1
a_root_parent 1
a_root_visible 1
a_count 1
a_find_root 1
a_identity_scale 1
b_idx_a 1
b_idx_b 1
b_idx_a1 1
b_idx_a2 1
b_idx_b1 1
b_parent_a1 1
b_name_b1 1
b_depth_b1 1
b_children_root 1
b_children_a 1
b_child_at_root_1 1
c_traverse_ok 1
c_traverse_count 1
traverse 0
traverse 1
traverse 3
traverse 4
traverse 2
traverse 5
c_traverse_order 1
c_subtree_ok 1
c_subtree_count 1
subtree 1
subtree 3
subtree 4
c_subtree_order 1
d_transform_set 1
d_transform_get 1
d_transform_roundtrip 1
d_visible_off 1
d_visible_on 1
d_bad_transform_get 1
d_bad_visible 1
e_duplicate 1
e_oversized_name 1
e_invalid_parent 1
e_self_cycle 1
e_descendant_cycle 1
e_remove_root 1
e_small_buffer 1
e_bad_find 1
e_sc2_init 1
e_chain_depth 1
e_depth_refusal 1
f_reparent 1
f_new_parent 1
f_old_parent_children 1
f_new_parent_children 1
f_traverse_ok 1
f_traverse_count 1
reparent 0
reparent 1
reparent 3
reparent 4
reparent 5
reparent 2
f_traverse_order 1
f_root_fixed 1
g_remove_leaf 1
g_leaf_gone 1
g_live_count 1
g_parent_children 1
g_tombstone_transform 1
g_remove_subtree 1
g_subtree_gone 1
g_count_after 1
g_traverse_ok 1
after_remove 0
after_remove 2
g_traverse_order 1
h_bad_init 1
h_bad_init_clean 1
h_si_init 1
h_add_alloc 1
h_add_state 1
h_kid_added 1
h_remove_alloc1 1
h_remove_still_alive 1
h_remove_alloc2 1
h_remove_alloc2_state 1
i_box_init 1
i_box_add 1
i_box_transform 1
i_box_remove 1
i_canary_head 1
i_canary_tail 1
i_canary_after_drop 1
z_meta_balance 1
SCENE3D 79 0
EOF

printf 'build: %s\n' "$CPLUS build tests/stdlib/scene3d/test_scene3d.cp --default --sanitize=none -I. -- build/libc/libcplus_hardened.a"
timeout -k "$KILL_AFTER" "$BUILD_TIMEOUT" "$CPLUS" build "$DIR/test_scene3d.cp" \
  --default --sanitize=none -I. -o "$BIN" -- build/libc/libcplus_hardened.a \
  >"$BLOG" 2>&1
brc=$?
if [ "$brc" -ne 0 ]; then
  printf 'BUILD-FAIL rc=%s\n' "$brc"
  tail -n 10 "$BLOG" | sed 's/^/  /'
  exit 1
fi
printf 'build: ok\n'

timeout -k "$KILL_AFTER" "$RUN_TIMEOUT" "$BIN" </dev/null >"$SO" 2>"$SE"
rrc=$?
if [ "$rrc" -ne 0 ]; then
  printf 'RUN-FAIL rc=%s\n' "$rrc"
  head -n 5 "$SE" | sed 's/^/  /'
  exit 1
fi
if [ -s "$SE" ]; then
  printf 'RUN-FAIL stderr not empty:\n'
  head -n 5 "$SE" | sed 's/^/  /'
  exit 1
fi

final=$(tail -n 1 "$SO")
case "$final" in
  "SCENE3D "*) ;;
  *) printf 'RUN-FAIL bad summary: %s\n' "$final"; exit 1 ;;
esac
rest=${final#SCENE3D }
declared_checks=${rest%% *}
declared_failures=${rest##* }
case "$declared_checks" in ''|*[!0-9]*) printf 'RUN-FAIL bad check count\n'; exit 1 ;; esac
case "$declared_failures" in ''|*[!0-9]*) printf 'RUN-FAIL bad failure count\n'; exit 1 ;; esac
if [ "$declared_failures" -ne 0 ]; then
  printf 'RUN-FAIL %s check(s) failed\n' "$declared_failures"
  exit 1
fi
# Check tags are single-letter-prefixed ("a_init", "z_meta_balance", ...);
# value lines such as "traverse 0" are observations, not check results.
observed_tags=$(awk 'NF==2 && $1 ~ /^[a-z]_[a-z0-9_]*$/ && ($2=="0" || $2=="1"){n++; if($2=="0") bad++} END{print n+0 "," bad+0}' "$SO")
tag_count=${observed_tags%%,*}
bad_count=${observed_tags##*,}
if [ "$tag_count" -ne "$declared_checks" ]; then
  printf 'RUN-FAIL tag count %s != declared %s\n' "$tag_count" "$declared_checks"
  exit 1
fi
if [ "$bad_count" -ne 0 ]; then
  printf 'RUN-FAIL %s check line(s) printed 0\n' "$bad_count"
  exit 1
fi
if ! cmp -s "$SO" "$EXPECTED"; then
  printf 'RUN-FAIL stdout differs from expected transcript\n'
  diff -u "$EXPECTED" "$SO" | head -n 30 | sed 's/^/  /'
  exit 1
fi

printf 'scene3d: ok (%s checks, exact transcript matched, stderr empty)\n' "$declared_checks"
exit 0
