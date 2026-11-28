#!/bin/sh
# Deterministic adversarial fuzz for the C+ SAFE subset.
# Shell-only equivalent of the former tests/core/adversarial_fuzz.py.
# Generates parameterised positive and negative programs, runs `c+ check`,
# asserts every negative case is rejected (with the expected CPLUS-* category
# where the checker owns it) and every positive case is accepted. Mismatches
# are saved under tests/out/core/adversarial/failures/ for regression triage.
# This is evidence for the declared subset, not a proof of arbitrary-C safety.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CC=${CPLUS_BIN:-$ROOT/build/c+}
OUT=$ROOT/tests/out/core/adversarial
FAILS=$OUT/failures
MSGS=$OUT/_failures.txt
mkdir -p "$OUT" "$FAILS"
: > "$MSGS"

unsafe=0
positive=0
failures=0
shown=0

run_case() {
  label=$1 path=$2 expect=$3 category=$4
  if diag=$("$CC" check "$path" 2>&1); then rc=0; else rc=$?; fi
  rejected=no
  [ "$rc" -ne 0 ] && rejected=yes
  if [ "$expect" = reject ]; then want=yes; else want=no; fi
  msg=
  if [ "$rejected" != "$want" ]; then
    cp "$path" "$FAILS/$label.c"
    d=$(printf '%s' "$diag" | head -c 300)
    msg="MISMATCH $label: expected $expect, rc=$rc
$d"
  elif [ "$expect" = reject ] && [ -n "$category" ]; then
    case "$diag" in
      *"$category"*) ;;
      *)
        cp "$path" "$FAILS/$label.c"
        d=$(printf '%s' "$diag" | head -c 300)
        msg="CATEGORY $label: expected $category
$d"
        ;;
    esac
  fi
  if [ -n "$msg" ]; then
    failures=$((failures + 1))
    if [ "$shown" -lt 20 ]; then
      printf '%s\n@@END@@\n' "$msg" >> "$MSGS"
      shown=$((shown + 1))
    fi
  fi
}

add_unsafe() {
  name=$1 category=$2
  label=${name}_${n}_${ui}
  path=$OUT/$label.c
  cat > "$path"
  run_case "$label" "$path" reject "$category"
  ui=$((ui + 1))
  unsafe=$((unsafe + 1))
}

add_positive() {
  name=$1
  label=${name}_${n}_${pi}
  path=$OUT/$label.c
  cat > "$path"
  run_case "$label" "$path" accept ""
  pi=$((pi + 1))
  positive=$((positive + 1))
}

prepare() {
  overflow=$(printf '%s' abcdefghij | cut -c1-"$n")
  checked=$(printf '%s' abcdefghi | cut -c1-"$n")
  case $n in
    2) ok=a; oklen=1; m=2 ;;
    3) ok=ab; oklen=2; m=3 ;;
    *) ok=abc; oklen=3; m=4 ;;
  esac
}

# Unsafe cases first (all n), then positive cases: preserves the original
# script's failure-report ordering.
ui=0
for n in 2 3 4 5 6 7 8 9; do
  prepare
  add_unsafe uaf_direct CPLUS-LIFETIME <<EOF
#include <stdlib.h>
int main(void){ int *p=malloc(${n}*sizeof(int)); free(p); return *p; }
EOF
  add_unsafe uaf_alias CPLUS-LIFETIME <<EOF
#include <stdlib.h>
int main(void){ int *p=malloc(${n}*sizeof(int)); int *q=p; free(p); return q[0]; }
EOF
  add_unsafe double_free CPLUS-OWNER <<EOF
#include <stdlib.h>
int main(void){ int *p=malloc(${n}); free(p); free(p); return 0; }
EOF
  add_unsafe double_free_alias CPLUS-OWNER <<EOF
#include <stdlib.h>
int main(void){ int *p=malloc(${n}); int *q=p; free(p); free(q); return 0; }
EOF
  add_unsafe oob_const CPLUS-BOUNDS <<EOF
int main(void){ int a[${n}]={0}; return a[$((n+1))]; }
EOF
  add_unsafe oob_loop CPLUS-BOUNDS <<EOF
int main(void){ int a[${n}]={0}; for(int i=0;i<$((n+2));i++) a[i]=i; return 0; }
EOF
  add_unsafe oob_multidim CPLUS-BOUNDS <<EOF
int main(void){ int g[2][${n}]={0}; return g[1][$((n+1))]; }
EOF
  add_unsafe ptr_arith CPLUS-BOUNDS <<EOF
int main(void){ int a[${n}]={0}; int *p=a+$((n+2)); return *p; }
EOF
  add_unsafe null_deref CPLUS-LIFETIME <<EOF
int main(void){ int *p=0; for(int i=0;i<${n};i++) *p=i; return 0; }
EOF
  add_unsafe uninit_branch CPLUS-LIFETIME <<EOF
int main(int argc,char**argv){ int x; if(argc>${n}) x=3; return x; }
EOF
  add_unsafe strcpy_overflow CPLUS-BOUNDS <<EOF
#include <string.h>
int main(void){ char d[${n}]; strcpy(d,"${overflow}"); return 0; }
EOF
  add_unsafe memcpy_overflow CPLUS-BOUNDS <<EOF
#include <string.h>
int main(void){ char d[${n}]; char s[16]={0}; memcpy(d,s,$((n+2))); return 0; }
EOF
  add_unsafe checked_copy_bad CPLUS-LIBC <<EOF
#include "cplus/cplus.h"
int main(void){ char d[${n}]; return cplus_strcpy(d,2,"${checked}"); }
EOF
  add_unsafe realloc_alias CPLUS-LIFETIME <<EOF
#include <stdlib.h>
int main(void){ int *p=malloc(${n}); int *q=p; void *t=realloc(p,${n}*2); if(t){p=t;return *q;} free(p); return 0; }
EOF
  add_unsafe resource_copy "" <<EOF
resource struct R { int v[${n}]; };
void R_drop(R*r){ if(r->v[0]) r->v[0]=0; }
int main(void){ R a={{1}}; R b=a; (void)b; return 0; }
EOF
  add_unsafe managed_escape "" <<EOF
#include "cplus/cplus.h"
int *bad(void){ managed int *p=cplus_new_array(int,${n}); return p; }
int main(void){ (void)bad(); return 0; }
EOF
  add_unsafe stack_return "" <<EOF
int *bad(void){ int local[${n}]; return &local[0]; }
int main(void){ (void)bad(); return 0; }
EOF
  add_unsafe countof_ptr "" <<EOF
#include "cplus/cplus.h"
size_t f(int *p){ return countof(p)+${n}; }
int main(void){ return 0; }
EOF
done

pi=0
for n in 2 3 4 5 6 7 8 9; do
  prepare
  add_positive malloc_ok <<EOF
#include <stdlib.h>
int main(void){ int *p=malloc(${n}*sizeof(int)); if(!p)return 1; p[0]=${n}; int v=p[0]; free(p); return v==${n}?0:1; }
EOF
  add_positive alias_before_free <<EOF
#include <stdlib.h>
int main(void){ int *p=malloc(${n}); int *q=p; *q=7; int v=*p; free(p); return v==7?0:1; }
EOF
  add_positive realloc_safe <<EOF
#include <stdlib.h>
int main(void){ int *p=malloc(${n}); void *t=realloc(p,${n}*2); if(t) p=t; if(!p)return 1; free(p); return 0; }
EOF
  add_positive loop_ok <<EOF
int main(void){ int a[${n}]={0}; for(int i=0;i<${n};i++) a[i]=i; return a[$((n-1))]==$((n-1))?0:1; }
EOF
  add_positive null_check <<EOF
#include <stdlib.h>
int main(void){ int *p=malloc(${n}); if(!p)return 1; *p=3; free(p); return 0; }
EOF
  add_positive init_all_branches <<EOF
int main(int argc,char**argv){ int x; if(argc>${n}) x=1; else x=2; return x>0?0:1; }
EOF
  add_positive alias_writes <<EOF
struct S { int x[${n}]; };
int main(void){ struct S s={0}; int *a=&s.x[0]; int *b=&s.x[1]; *a=1; *b=2; return s.x[0]==1?0:1; }
EOF
  add_positive strcpy_ok <<EOF
#include <string.h>
int main(void){ char d[${n}]; strcpy(d,"${ok}"); return d[0]=='a'?0:1; }
EOF
  add_positive memcpy_ok <<EOF
#include <string.h>
int main(void){ char d[${n}]; char s[4]={'a','b','c',0}; memcpy(d,s,${m}); return d[0]=='a'?0:1; }
EOF
  add_positive checked_copy_ok <<EOF
#include "cplus/cplus.h"
int main(void){ char d[${n}]; return cplus_strcpy(d,sizeof(d),"${ok}")==${oklen}?0:1; }
EOF
done

printf 'adversarial fuzz: %s unsafe, %s positive, %s failures\n' "$unsafe" "$positive" "$failures"
awk '/^@@END@@$/ { n++; if (n >= 20) exit; next } { print }' "$MSGS"
if [ "$failures" -gt 0 ]; then
  printf 'saved fixtures in %s\n' "$FAILS"
  exit 1
fi
