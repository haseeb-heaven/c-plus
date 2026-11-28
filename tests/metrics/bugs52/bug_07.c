#include <stdio.h>
int* get_stack_ptr(void) { int local = 42; return &local; }
int main(void) { int* p = get_stack_ptr(); printf("%d\n", *p); return 0; }
