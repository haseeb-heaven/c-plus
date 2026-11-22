/* Catalog bug 575: strtok static state invalidates an earlier result. */
#include <stdio.h>
#include <string.h>
int main(void) {
    char a[] = "a,b,c", b[] = "x,y,z";
    char *first = strtok(a, ",");
    char *reset = strtok(b, ",");
    printf("%s %s\n", first, reset); /* first is invalidated */
    return 0;
}
