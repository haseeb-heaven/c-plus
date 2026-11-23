#include <stdio.h>

#define TYPEOF(x) _Generic((x), int: "int", char: "char", double: "double", default: "other")

int main(void) {
    int i = 1;
    char c = 'a';
    double d = 1.0;
    long l = 1;
    printf("%s %s %s %s\n", TYPEOF(i), TYPEOF(c), TYPEOF(d), TYPEOF(l));
    return 0;
}
