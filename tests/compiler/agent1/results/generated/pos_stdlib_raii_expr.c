#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/stdlib_raii_expr.cp"
#include "/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/cplus-hp-80244-0.h"
#include "/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/cplus-hp-80244-1.h"
int main(void) {
  String s = String_ctor("hello");
  IntVec v = IntVec_ctor();
  for (__typeof__( 0) i = 0; i < 20; i++) IntVec_push(&v, i * i);
  { __typeof__(String_len ( & s ) == 5 && String_at ( & s , 1 ) == 'e' && IntVec_len ( & v ) == 20 && IntVec_at ( & v , 19 ) == 361 ? 0 : 1) __cplus_ret = (String_len ( & s ) == 5 && String_at ( & s , 1 ) == 'e' && IntVec_len ( & v ) == 20 && IntVec_at ( & v , 19 ) == 361 ? 0 : 1); IntVec_drop(&v); String_drop(&s);  return __cplus_ret;; }
IntVec_drop(&v); String_drop(&s); 
#line 9 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/stdlib_raii_expr.cp"
}