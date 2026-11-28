#!/bin/sh
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CC=${CPLUS_BIN:-$ROOT/build/c+}
P="$ROOT/tests/out/core/probes"
mkdir -p "$P"
PASS=0; FAIL=0
run() {
  n=$1; shift
  if "$@" >"$P/$n.log" 2>&1; then echo "PASS $n"; PASS=$((PASS+1)); else echo "FAIL $n"; FAIL=$((FAIL+1)); fi
}
expect_fail() {
  n=$1; shift
  if "$@" >"$P/$n.log" 2>&1; then echo "FAIL $n (accepted)"; FAIL=$((FAIL+1)); else echo "PASS $n"; PASS=$((PASS+1)); fi
}
cat > "$P/raii_order.cp" <<'S'
#include "cplus/cplus.h"
static int seq=0;
resource struct R { int id; };
void R_drop(R *r){ if(r->id){seq=seq*10+r->id;r->id=0;} }
int main(void){ {R a={1};R b={2};R c={3};} return seq==321?0:1; }
S
cat > "$P/early_return.cp" <<'S'
#include "cplus/cplus.h"
static int n=0; resource struct R{int id;}; void R_drop(R*r){if(r->id){n++;r->id=0;}}
static int f(int x){R r={1}; if(x) return 5; return 7;}
int main(void){return f(1)==5&&n==1?0:1;}
S
cat > "$P/move_return.cp" <<'S'
#include "cplus/cplus.h"
resource struct R{int id;}; void R_drop(R*r){if(r->id)r->id=0;}
static R make(void){R r={9}; return r;}
int main(void){return make().id==9?0:1;}
S
cat > "$P/bad_bounds.c" <<'S'
int main(void){int a[3]={0};return a[5];}
S
cat > "$P/bad_uninit.c" <<'S'
int main(void){int x; return x;}
S
cat > "$P/bad_null.c" <<'S'
int main(void){int*p=0; return *p;}
S
cat > "$P/double_free.cp" <<'S'
#include "cplus/cplus.h"
int main(void){int*p=cplus_new(int);cplus_release(p);cplus_release(p);return 0;}
S
cat > "$P/freed_len.cp" <<'S'
#include "cplus/cplus.h"
int main(void){int*p=cplus_new_array(int,2);cplus_release(p);return (int)p.len;}
S
cat > "$P/bad_ptr_arith.c" <<'S'
int main(void){int a[4]={0}; int*p=a+8; return *p;}
S
run raii_order "$CC" build "$P/raii_order.cp" -o "$P/raii_order" &&
  run raii_order_run "$P/raii_order"
run early_return "$CC" build "$P/early_return.cp" -o "$P/early_return" &&
  run early_return_run "$P/early_return"
run move_return "$CC" build "$P/move_return.cp" -o "$P/move_return" &&
  run move_return_run "$P/move_return"
expect_fail reject_bounds "$CC" check "$P/bad_bounds.c"
expect_fail reject_uninit "$CC" check "$P/bad_uninit.c"
expect_fail reject_null "$CC" check "$P/bad_null.c"
expect_fail reject_ptr_arith "$CC" check "$P/bad_ptr_arith.c"
run double_free_build "$CC" build "$P/double_free.cp" -o "$P/double_free"
"$P/double_free" >/dev/null 2>&1
rc=$?; if [ "$rc" -eq 104 ]; then echo "PASS double_free_trap"; PASS=$((PASS+1)); else echo "FAIL double_free_trap rc=$rc"; FAIL=$((FAIL+1)); fi
run freed_len_build "$CC" build "$P/freed_len.cp" -o "$P/freed_len"
"$P/freed_len" >/dev/null 2>&1
rc=$?; if [ "$rc" -eq 105 ]; then echo "PASS freed_len_trap"; PASS=$((PASS+1)); else echo "FAIL freed_len_trap rc=$rc"; FAIL=$((FAIL+1)); fi
echo
echo "core safety probes: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
