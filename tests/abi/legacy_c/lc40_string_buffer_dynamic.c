/*
 * lc40 - string buffers: two-pass snprintf sizing into a heap buffer,
 * including truncation detection. Independent legacy-C ABI probe.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    const char *label = "lc40";
    int a = 12, b = 34;
    char small[8];
    char *dynamic = NULL;
    int needed, written;
    int ok = 1;

    /* Pass 1: measure into a deliberately too-small stack buffer. */
    needed = snprintf(small, sizeof(small), "%s/%d-%d/payload", label, a, b);
    if (needed < 0 || (size_t)needed <= sizeof(small)) {
        printf("lc40 result: FAIL sizing needed=%d\n", needed);
        return 1;
    }
    if (strcmp(small, "lc40/12") != 0) {
        ok = 0; /* truncated prefix is deterministic across libc's */
    }
    /* Pass 2: allocate exact size and format in full. */
    dynamic = (char *)malloc((size_t)needed + 1u);
    if (dynamic == NULL) {
        printf("lc40 result: FAIL malloc\n");
        return 1;
    }
    written = snprintf(dynamic, (size_t)needed + 1u,
                       "%s/%d-%d/payload", label, a, b);
    if (written != needed || strcmp(dynamic, "lc40/12-34/payload") != 0) {
        ok = 0;
    }
    free(dynamic);
    if (!ok) {
        printf("lc40 result: FAIL needed=%d written=%d\n", needed, written);
        return 1;
    }
    printf("lc40 result: OK needed=%d text='lc40/12-34/payload'\n", needed);
    return 0;
}
