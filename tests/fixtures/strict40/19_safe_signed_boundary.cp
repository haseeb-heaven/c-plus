#include <limits.h>
int main(void) { int a = INT_MAX - 1; int b = a + 1; int c = INT_MIN + 1; int d = c - 1; return (b == INT_MAX && d == INT_MIN) ? 0 : 1; }
