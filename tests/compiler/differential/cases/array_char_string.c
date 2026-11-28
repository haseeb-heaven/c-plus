#include <stdio.h>
#include <string.h>
int main(void) {
    char buf[32] = "prefix";
    strcat(buf, "-tag587576");
    printf("str=%s len=%zu first=%c last=%c\n", buf, strlen(buf), buf[0],
           buf[strlen(buf) - 1]);
    return 0;
}
