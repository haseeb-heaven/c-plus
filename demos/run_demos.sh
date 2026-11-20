#!/bin/sh
# Build and run the C+ demos.
#
# Usage: demos/run_demos.sh
#
# Demo B is expected to fail: it performs an out-of-bounds read, which safe C+
# detects and turns into a clean abort instead of memory corruption.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$ROOT/demos/out"
PASS=0
FAIL=0
FAILED=""

mkdir -p "$OUT"

if [ ! -x "$CPLUS" ]; then
  echo "error: C+ compiler not found at $CPLUS (build with cmake --build build)" >&2
  exit 1
fi

pass() {
  PASS=$((PASS + 1))
  printf '  ok   %s\n' "$1"
}

fail() {
  FAIL=$((FAIL + 1))
  FAILED="$FAILED
  - $1: $2"
  printf '  FAIL %s (%s)\n' "$1" "$2"
}

build() {
  name=$1
  "$CPLUS" build "$ROOT/demos/$name.c" -o "$OUT/$name" \
    >"$OUT/$name.build.log" 2>&1
}

# expect_ok <name> <expected stdout file>
expect_ok() {
  name=$1
  expected=$2
  if ! build "$name"; then
    fail "$name" "build failed; see demos/out/$name.build.log"
    return
  fi
  "$OUT/$name" >"$OUT/$name.stdout" 2>"$OUT/$name.stderr"
  rc=$?
  if [ "$rc" -ne 0 ]; then
    fail "$name" "exited with $rc; see demos/out/$name.stderr"
    return
  fi
  if [ -f "$expected" ] &&
     ! diff -u "$expected" "$OUT/$name.stdout" >"$OUT/$name.diff" 2>&1; then
    fail "$name" "output mismatch; see demos/out/$name.diff"
    return
  fi
  pass "$name"
}

echo "C+ demos"
echo

# A: ordinary C program.
printf 'Hello from C+\n' >"$OUT/A_hello.expected"
expect_ok A_hello "$OUT/A_hello.expected"

# B: memory-safety violation must abort with a diagnostic, never continue.
if ! build B_bounds; then
  fail B_bounds "build failed; see demos/out/B_bounds.build.log"
else
  # The abort is intentional.  Run the demo in a redirected group so the
  # shell's own "Abort trap" job message does not pollute the report.
  rm -f "$OUT/B_bounds.exit"
  { "$OUT/B_bounds" >"$OUT/B_bounds.stdout" 2>"$OUT/B_bounds.stderr";
    echo $? >"$OUT/B_bounds.exit"; } 2>/dev/null
  rc=$(cat "$OUT/B_bounds.exit")
  if [ "$rc" -eq 0 ]; then
    fail B_bounds "out-of-bounds read did not abort"
  elif ! grep -q "out of bounds" "$OUT/B_bounds.stderr"; then
    fail B_bounds "no bounds diagnostic; see demos/out/B_bounds.stderr"
  fi
  pass B_bounds
fi

# C: deterministic cleanup on normal and early return.
cat >"$OUT/C_resource.expected" <<'EOF'
normal return
close log
run(0)=0
early return
close log
run(1)=1
EOF
expect_ok C_resource "$OUT/C_resource.expected"

# D: class lowering and method dispatch.
cat >"$OUT/D_class.expected" <<'EOF'
after damage: 70
after heal: 80
through pointer: 75
EOF
expect_ok D_class "$OUT/D_class.expected"

# E: sockets + RAII connection cleanup.  Output order between the forked
# client and the server is not deterministic, so check the lines.
if ! build E_networking; then
  fail E_networking "build failed; see demos/out/E_networking.build.log"
else
  "$OUT/E_networking" >"$OUT/E_networking.stdout" 2>"$OUT/E_networking.stderr"
  rc=$?
  if [ "$rc" -ne 0 ]; then
    fail E_networking "exited with $rc; see demos/out/E_networking.stderr"
  elif ! grep -q "listening on 127.0.0.1:" "$OUT/E_networking.stdout"; then
    fail E_networking "server did not listen"
  elif ! grep -q "server received: GET / HTTP/1.0" "$OUT/E_networking.stdout"; then
    fail E_networking "server did not receive the request"
  elif ! grep -q "client received: HTTP/1.0 200 OK" "$OUT/E_networking.stdout"; then
    fail E_networking "client did not receive the response"
  else
    pass E_networking
  fi
fi

# F: three threads, RAII-guarded mutex, no lost increments.
printf 'counter=150000\n' >"$OUT/F_concurrency.expected"
expect_ok F_concurrency "$OUT/F_concurrency.expected"

# G: a valid borrow of a scoped resource.
cat >"$OUT/G_borrow.expected" <<'EOF'
borrowed value: 42
owner still alive
drop slot 42
EOF
expect_ok G_borrow "$OUT/G_borrow.expected"

# H: managed allocation with tracked length and a valid borrowed view.
printf 'len=4 total=100\n' >"$OUT/H_allocation_borrow.expected"
expect_ok H_allocation_borrow "$OUT/H_allocation_borrow.expected"

# I: cleanup on normal exit, continue and early return.
cat >"$OUT/I_raii_safe.expected" <<'EOF'
drop 1
drop 2
drop 3
drop 2
early=-1
EOF
expect_ok I_raii_safe "$OUT/I_raii_safe.expected"

echo
echo "C+ demos: $PASS passed, $FAIL failed"
if [ "$FAIL" -ne 0 ]; then
  printf '%s\n' "$FAILED"
  exit 1
fi
exit 0
