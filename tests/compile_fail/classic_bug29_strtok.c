/* Bug 29: strtok internal static state. */
#include <stdio.h>
#include <string.h>
int main(void) {
    char s1[] = "a,b", s2[] = "x,y";
    char *a = strtok(s1, ",");
    char *b = strtok(s2, ",");
    char *c = strtok(NULL, ",");
    printf("%s %s %s\n", a, b, c);
    return 0;
}
