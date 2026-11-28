#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/null_return_guard.c"
static int *bad(void){ return 0; }
int main(void){ int *p = bad(); if(p) return *p; return 0; }
