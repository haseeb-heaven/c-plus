/* Deterministic non-zero exit code with flushed stdout. */
#include <stdio.h>

int main(void)
{
    printf("before-exit\n");
    return 7;
}
