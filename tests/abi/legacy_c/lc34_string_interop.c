/*
 * lc34 - C library interop: string.h (strlen, strcat, strcmp, strchr,
 * memcpy, memmove, memset, memcmp). Independent legacy-C ABI probe.
 */
#include <stdio.h>
#include <string.h>

int main(void)
{
    char buf[48] = "lc34";
    char copy[48];
    int len, cmp, found;

    (void)strcat(buf, "-");
    (void)strcat(buf, "string");
    len = (int)strlen(buf);
    cmp = strcmp(buf, "lc34-string");
    found = strchr(buf, '-') != NULL;

    memcpy(copy, buf, (size_t)len + 1u);
    if (memcmp(copy, buf, (size_t)len + 1u) != 0) {
        printf("lc34 result: FAIL memcpy\n");
        return 1;
    }
    memmove(copy + 1, copy, (size_t)len + 1u);
    copy[0] = 'X';
    memset(copy + len + 1, 0, 1);

    if (len != 11 || cmp != 0 || !found || copy[0] != 'X' ||
        strcmp(copy + 1, buf) != 0) {
        printf("lc34 result: FAIL len=%d cmp=%d found=%d\n", len, cmp, found);
        return 1;
    }
    printf("lc34 result: OK text='%s' len=%d move='%s'\n", buf, len, copy);
    return 0;
}
