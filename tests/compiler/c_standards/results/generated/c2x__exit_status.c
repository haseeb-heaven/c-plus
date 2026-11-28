#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/c_standards/fixtures/exit_status.c"
/* Deterministic non-zero exit code with flushed stdout. */
#include <stdio.h>

int main(void)
{
    printf("before-exit\n");
    return 7;
}
