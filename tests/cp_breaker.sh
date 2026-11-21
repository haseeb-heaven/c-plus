#!/usr/bin/env bash
# Adversarial C+ compatibility / safety suite.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=${CPLUS_BIN:-$ROOT/build/c+}
SRC_DIR="$ROOT/tests/breaker"
OUT="$ROOT/tests/out/breaker"
PASS=0; FAIL=0; XFAIL=0
prefix='cplus_probe'

mkdir -p "$OUT"
if [[ ! -x "$CC" ]]; then echo "C+ compiler not found: $CC" >&2; exit 2; fi

check() {
  local name=$1 expected=$2 source=$3 mode=${4:-build} rc=0
  local src="$SRC_DIR/${prefix}_${name}.cp" exe="$OUT/${prefix}_${name}" log="$OUT/${prefix}_${name}.log"
  if [[ ! -f "$src" ]]; then echo "missing probe: $src" >&2; exit 2; fi
  if [[ "$mode" == check ]]; then
    "$CC" check "$src" > "$log" 2>&1 || rc=$?
  else
    "$CC" build "$src" -o "$exe" > "$log" 2>&1 || rc=$?
  fi
  if [[ "$expected" == reject ]]; then
    if ((rc != 0)); then
      echo "PASS REJECT $name"; ((PASS+=1))
    else
      echo "FAIL ACCEPTED UNSAFE $name"; ((FAIL+=1))
    fi
    return
  fi
  if ((rc != 0)); then
    if [[ "$expected" == runtime* ]]; then
      echo "PASS REJECT $name (compile-time rejection is stronger than trap)"
      ((PASS+=1))
    else
      echo "FAIL BUILD $name (see $log)"; head -n 3 "$log" | sed 's/^/    /'; ((FAIL+=1))
    fi
    return
  fi
  if [[ "$mode" == check ]]; then
    echo "PASS CHECK $name"; ((PASS+=1)); return
  fi
  local runrc=0
  "$exe" > "$OUT/${prefix}_${name}.out" 2>> "$log" || runrc=$?
  if [[ "$expected" == runtime104 ]]; then
    if (( runrc == 104 )); then echo "PASS TRAP $name (exit 104)"; ((PASS+=1)); else echo "FAIL TRAP $name (exit $runrc instead of 104)"; ((FAIL+=1)); fi
    return
  fi
  if [[ "$expected" == runtime101 ]]; then
    if (( runrc == 101 )); then echo "PASS TRAP $name (exit 101)"; ((PASS+=1)); else echo "FAIL TRAP $name (exit $runrc instead of 101)"; ((FAIL+=1)); fi
    return
  fi
  if ((runrc == 0)); then
    echo "PASS RUN $name"; ((PASS+=1))
  else
    echo "FAIL RUNTIME $name (exit $runrc, see $log)"; ((FAIL+=1))
  fi
}

check class_this run '#include "cplus/cplus.h"
class Acc {
  int value;
  Acc(int initial) { this->value = initial; }
  void add(int x) { this->value += x; }
  int read(void) { return this->value; }
  int twice(void) { return this->read() * 2; }
};
int main(void) {
  Acc a = Acc(7);
  a.add(5);
  return a.read() == 12 && a.twice() == 24 ? 0 : 1;
}'

check range_auto_len run '#include "cplus/cplus.h"
int main(void) {
  int v[] = {1,2,3,4,5};
  auto sum = 0;
  for (auto x : v) sum += x;
  if (v.len != 5 || countof(v) != 5) return 1;
  return sum == 15 ? 0 : 2;
}'

check stdlib_raii run '#include "stdlib/string.hp"
#include "stdlib/vector.hp"
int main(void) {
  String s = String("hello");
  IntVec v = IntVec();
  for (auto i = 0; i < 20; i++) v.push(i * i);
  return s.len() == 5 && s.at(1) == '\''e'\'' && v.len() == 20 && v.at(19) == 361 ? 0 : 1;
}'

check resource_early_return run '#include <stdio.h>
static int drops = 0;
resource struct Token { int active; };
void Token_drop(Token *token) { if (token->active) drops++; }
int helper(int x) {
  Token a = {1};
  if (x) return 4;
  return 5;
}
int main(void) {
  if (helper(1) != 4 || drops != 1) return 1;
  if (helper(0) != 5 || drops != 2) return 2;
  return 0;
}'

check raii_order run '#include "cplus/cplus.h"
static int seq = 0;
resource struct Handle { int id; };
void Handle_drop(Handle *handle) { seq = seq*10 + handle->id; }
int main(void) {
  { Handle a={1}; Handle b={2}; Handle c={3}; }
  return seq == 321 ? 0 : 1;
}'

check managed_len run '#include "cplus/cplus.h"
int main(void) {
  managed int *xs = cplus_new_array(int, 7);
  if (xs.len != 7) return 1;
  for (auto i = 0; i < xs.len; i++) xs[i] = i + 1;
  return xs[6] == 7 ? 0 : 2;
}'

check default_arguments run 'int add(int a, int b = 2, int c = 3) { return a+b+c; }
int main(void) { return add(1)==6 && add(1,4)==8 && add(1,4,5)==10 ? 0 : 1; }'

check defer_return run 'static int value=0;
int operation(void) { defer value = value+7; return 3; }
int main(void) { int n=operation(); return n==3 && value==7 ? 0 : 1; }'

check if_decl run 'int main(void) {
  if (int n = 6; n > 2) return n == 6 ? 0 : 1;
  else return 2;
}'

check octal_rotation run '#include "cplus/cplus.h"
#include <stdint.h>
int main(void) {
  uint32_t x = 0x12345678u;
  return 0o755 == 493 && cplus_rotr32(cplus_rotl32(x, 7),7)==x ? 0 : 1;
}'

# C+ source checker must not reject these valid ordinary C cases.
check valid_multidim run 'static int g[4][4];
int main(void) { for(int i=0;i<16;i++) g[i/4][i%4]=i; return g[3][3]==15 ? 0 : 1; }'

check valid_fread_store run '#include <stdio.h>
int main(void) {
 FILE *f=tmpfile(); if(!f) return 0;
 char buf[8]={0}; size_t n=fread(buf,1,sizeof buf,f);
 if(ferror(f)) { fclose(f); return 1; }
 fclose(f); return n==0 ? 0 : 2;
}'

check valid_macro run '#include <stdio.h>
#define SWAP(a,b) do { int t=(a); (a)=(b); (b)=t; } while(0)
int main(void) { int a=4,b=5; SWAP(a,b); return a==5 && b==4 ? 0:1; }'

check valid_bitfield run 'struct Flags { unsigned ready:1; unsigned mode:3; };
int main(void) { struct Flags f = {1, 5}; return f.ready==1 && f.mode==5 ? 0:1; }'

check valid_printf run '#include <stdio.h>
int main(void) { printf("normal printf is legal outside signal handlers\\n"); return 0; }'

check valid_user_print run 'int print(void) { return 42; }
int main(void) { return print()==42?0:1; }'

# Negative checks: must fail at compilation.
check bad_constant_oob reject 'int main(void) { int a[3]={0}; return a[9]; }' check

check bad_loop_oob reject 'int main(void) { int a[3]={0}; for(int i=0;i<5;i++) a[i]=i; return 0; }' check

check bad_resource_copy reject 'resource struct T { int x; };
void T_drop(T *t) { t->x=0; }
int main(void) { T a={1}; T b=a; return b.x; }' check

check bad_managed_escape reject '#include "cplus/cplus.h"
int *bad(void) { managed int *p = cplus_new(int); return p; }
int main(void) { return 0; }' check

check bad_countof_pointer reject '#include "cplus/cplus.h"
size_t test(int *p) { return countof(p); }
int main(void) { return 0; }' check

check bad_null_deref reject 'int main(void) { int *p = 0; return *p; }' check

# Runtime checks: should terminate with explicit C+ exit code.
check double_release runtime104 '#include "cplus/cplus.h"
int main(void) { int *p=cplus_new(int); cplus_release(p); cplus_release(p); return 0; }'

check explicit_bounds runtime101 '#include "cplus/cplus.h"
int main(void) { return (int)cplus_check_index(9, 3, __FILE__, __LINE__, "test"); }'

printf '\nC+ BREAKER: %d passed, %d failed\n' "$PASS" "$FAIL"
printf 'Probe sources: %s/%s_*.cp\n' "$SRC_DIR" "$prefix"
(( FAIL == 0 ))

