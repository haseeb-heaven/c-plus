#!/bin/sh
# Deterministic build-and-run harness for the four systems_2 teaching models.
#
#   sh apps/legacy_examples/expanded/systems_2/run.sh
#
# Each app is built from the repository root with the supported C+ driver in
# strict mode.  Every compiler/program invocation is bounded by `timeout 30`.
# Inputs are fixed argv sequences (no clock, threads, network, prompts or
# user paths).  A scenario passes only when the exit code is 0, stderr is
# empty, stdout matches the checked-in golden file byte-for-byte, and a second
# run with the same argv is byte-identical.
#
# Final line: SYSTEMS_2 <apps> <passed> <failed> <refused>
#   <refused> is the total number of REFUSE lines correctly emitted by the
#   hostile-case scenarios (explicit refusal evidence, not a safety proof).
set -u

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$DIR/../../../.." && pwd) || exit 2
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
RES="$DIR/results"
GOLDEN="$DIR/expected"
KILL_AFTER=30

PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
export PATH

mkdir -p "$RES" || exit 2
if [ ! -d "$GOLDEN" ]; then
  echo "error: golden directory missing: $GOLDEN" >&2
  exit 2
fi
if ! command -v timeout >/dev/null 2>&1; then
  echo "error: 'timeout' utility not found; refusing unbounded tests" >&2
  exit 2
fi
if [ ! -x "$CPLUS" ]; then
  echo "error: C+ compiler missing: $CPLUS" >&2
  exit 2
fi

cd "$ROOT" || exit 2

APPS=4
passed=0
failed=0
refused=0
app_failed=0

build_app() {
  name=$1
  timeout -k 5 "$KILL_AFTER" "$CPLUS" build "$DIR/$name.cp" \
    --strict --sanitize=none -I. -o "$RES/$name" \
    >"$RES/$name.build.log" 2>&1
  brc=$?
  if [ "$brc" -ne 0 ] || [ ! -x "$RES/$name" ]; then
    echo "BUILD-FAIL $name (rc=$brc)"
    tail -n 12 "$RES/$name.build.log" | sed 's/^/           /'
    app_failed=1
    return 1
  fi
  echo "build ok   $name"
  return 0
}

# run_case <label> <app> <golden-file> [fixed argv...]
run_case() {
  label=$1
  app=$2
  golden=$3
  shift 3
  rm -f "$RES/$label.out" "$RES/$label.err" \
        "$RES/$label.out2" "$RES/$label.err2"
  timeout -k 5 "$KILL_AFTER" "$RES/$app" "$@" \
    >"$RES/$label.out" 2>"$RES/$label.err" </dev/null
  rc=$?

  ok=1
  [ "$rc" -eq 0 ] || ok=0
  if [ -s "$RES/$label.err" ]; then ok=0; fi
  if ! cmp -s "$golden" "$RES/$label.out"; then ok=0; fi

  timeout -k 5 "$KILL_AFTER" "$RES/$app" "$@" \
    >"$RES/$label.out2" 2>"$RES/$label.err2" </dev/null
  rc2=$?
  [ "$rc2" -eq 0 ] || ok=0
  if [ -s "$RES/$label.err2" ]; then ok=0; fi
  if ! cmp -s "$RES/$label.out" "$RES/$label.out2"; then ok=0; fi

  nref=$(grep -c 'REFUSE' "$RES/$label.out" 2>/dev/null || true)
  refused=$((refused + nref))

  if [ "$ok" -eq 1 ]; then
    echo "ok         $label (refused=$nref)"
  else
    echo "FAIL       $label (rc=$rc rc2=$rc2)"
    diff -u "$golden" "$RES/$label.out" 2>/dev/null | head -n 24 \
      | sed 's/^/           /'
    head -n 6 "$RES/$label.err" | sed 's/^/           stderr: /'
    app_failed=1
  fi
}

finish_app() {
  if [ "$app_failed" -eq 1 ]; then
    failed=$((failed + 1))
  else
    passed=$((passed + 1))
  fi
  app_failed=0
}

# Fixed oversized tokens used only by hostile cases (lengths exceed the
# program's declared field caps): key=30>23, value=40>31, name=30>19,
# topic=32>19, subscriber=30>19, payload=40>39, batch=33>32.
LONGKEY=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
LONGVAL=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
LONGNAME=cccccccccccccccccccccccccccccc
LONGTOPIC=dddddddddddddddddddddddddddddddd
LONGSUB=eeeeeeeeeeeeeeeeeeeeeeeeeeeeee
LONGPAYLOAD=ffffffffffffffffffffffffffffffffffffffff
BIGBATCH="1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33"

echo "== lru_cache =="
if build_app lru_cache; then
  run_case lru_flow lru_cache "$GOLDEN/lru_flow.expected" \
    cap 3 -- put alpha 1 -- put beta 2 -- put gamma 3 -- get alpha \
    -- put delta 4 -- dump -- expect alpha 1 -- expect gamma 3 -- expect delta 4
  run_case lru_hostile lru_cache "$GOLDEN/lru_hostile.expected" \
    cap 0 -- cap 99 -- put k v -- put "$LONGKEY" v -- put k "$LONGVAL" \
    -- put '' v -- get missing -- bogus
fi
finish_app

echo "== scheduler =="
if build_app scheduler; then
  run_case sch_flow scheduler "$GOLDEN/sch_flow.expected" \
    capacity 3 -- arrive net 0 2 -- arrive disk 1 1 -- arrive ui 0 1 \
    -- expect net 2 -- expect disk 1 -- run 4 -- dump
  run_case sch_hostile scheduler "$GOLDEN/sch_hostile.expected" \
    capacity 0 -- tick -- capacity 2 -- arrive a 0 1 -- arrive a 0 1 \
    -- arrive b 0 1 -- arrive c 0 1 -- capacity 99 -- arrive x 2 1 \
    -- arrive y 0 0 -- arrive "$LONGNAME" 0 1 -- run 0 -- bogus
fi
finish_app

echo "== event_bus =="
if build_app event_bus; then
  run_case bus_flow event_bus "$GOLDEN/bus_flow.expected" \
    topic room -- sub room alice -- sub room bob -- pub room hello \
    -- expect room alice -- unsub room bob -- pub room bye -- dump
  run_case bus_hostile event_bus "$GOLDEN/bus_hostile.expected" \
    topic room -- topic room -- sub room alice -- sub room alice \
    -- pub room "$LONGPAYLOAD" -- pub ghost x -- unsub room nobody \
    -- sub "$LONGTOPIC" who -- sub room "$LONGSUB" -- bogus
fi
finish_app

echo "== ring_buffer =="
if build_app ring_buffer; then
  run_case rb_flow ring_buffer "$GOLDEN/rb_flow.expected" \
    cap 4 -- push 1 2 3 4 -- pop 2 -- push 5 6 -- dump -- expect 4 3 4 5 6 \
    -- peek 2
  run_case rb_hostile ring_buffer "$GOLDEN/rb_hostile.expected" \
    cap 0 -- push 1 -- cap 4 -- push 1 2 3 4 -- push 9 -- pop 9 -- peek 0 \
    -- push 300 -- cap 99 -- push $BIGBATCH -- bogus
fi
finish_app

printf 'SYSTEMS_2 %d %d %d %d\n' "$APPS" "$passed" "$failed" "$refused"
[ "$failed" -eq 0 ]
