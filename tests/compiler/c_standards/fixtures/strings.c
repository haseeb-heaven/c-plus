/* Deterministic string handling; valid in every C standard under test. */
#include <stdio.h>
#include <string.h>

int main(void)
{
    char buf[32];
    const char *s = "legacy-c";

    strcpy(buf, s);
    strcat(buf, "-ok");
    printf("len=%lu cmp=%d\n", (unsigned long)strlen(buf),
           strcmp(buf, "legacy-c-ok"));
    printf("buf=%s\n", buf);
    printf("first=%c last=%c\n", buf[0], buf[strlen(buf) - 1]);
    return 0;
}
