#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/pointer_one_past.c"
int main(void) { int a[4] = {0}; int *p = a + 4; return p == a + 4 ? 0 : 1; }