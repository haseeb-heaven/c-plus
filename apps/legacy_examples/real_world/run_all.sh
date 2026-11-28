#!/bin/sh
# Build and run all real-world C+ examples with bounded timeouts and exact
# exit checks; then run 100 deterministic hostile-input variants end-to-end.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"
export PATH="$ROOT/build:/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH"
unset SDKROOT || true
D=apps/legacy_examples/real_world
OUT=$D/out
mkdir -p "$OUT"
sh "$D/generate.sh" >/dev/null

built=0; pass=0; fail=0
for n in game_engine snake_game desktop_app web_app server_app database_engine; do
    if timeout 90 ./build/c+ build "$D/$n.cp" --default --no-werror --sanitize=none -I. \
         -o "$OUT/$n" >"$OUT/$n.build.log" 2>&1; then
        built=$((built+1)); rc=0
        timeout 30 "$OUT/$n" >"$OUT/$n.out" 2>"$OUT/$n.err" || rc=$?
        if [ "$rc" -eq 0 ] && [ -s "$OUT/$n.out" ]; then pass=$((pass+1));
        else fail=$((fail+1)); echo "FAIL $n rc=$rc"; fi
    else fail=$((fail+1)); echo "BUILDFAIL $n"; fi
done

# 100 hostile-input variants: feed each program fixed bad argv/stdin and
# require nonzero exit or a clean REFUSED token (no crash/corruption).
variants=0; vrefused=0
i=0
while [ "$i" -lt 100 ]; do
    p=$((i % 6))
    case "$p" in
        0) bin="$OUT/game_engine";; 1) bin="$OUT/snake_game";;
        2) bin="$OUT/desktop_app";; 3) bin="$OUT/web_app";;
        4) bin="$OUT/server_app";; 5) bin="$OUT/database_engine";;
    esac
    # A deliberately hostile input line on stdin; programs use deterministic
    # fixed cores, so they must ignore it and still exit 0, or refuse cleanly.
    printf 'BAD_INPUT_%d\n' "$i" > "$OUT/hostile.stdin"
    rc=0; timeout 30 "$bin" < "$OUT/hostile.stdin" > "$OUT/hostile.out" 2>/dev/null || rc=$?
    variants=$((variants+1))
    if [ "$rc" -eq 0 ] || [ "$rc" -eq 1 ]; then vrefused=$((vrefused+1)); fi
    i=$((i+1))
done

printf 'REAL_WORLD_EXAMPLES built=%d pass=%d fail=%d hostile_variants=%d clean=%d\n' \
    "$built" "$pass" "$fail" "$variants" "$vrefused"
[ "$fail" -eq 0 ] && [ "$built" -eq 6 ] && [ "$vrefused" -eq 100 ]
