#include <stdio.h>
int main(void) {
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%s-%d-%d", "abcdefghij", 1520, 5834);
    printf("n=%d buf=%s\n", n, buf);
    printf("len=%zu\n", sizeof(buf));
    return 0;
}
