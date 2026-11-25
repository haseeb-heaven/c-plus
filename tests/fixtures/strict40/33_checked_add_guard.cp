#include <limits.h>
int checked_add(int a, int b, int *out) { if (!out) return 0; if ((b > 0 && a > INT_MAX - b) || (b < 0 && a < INT_MIN - b)) return 0; *out = a + b; return 1; }
int main(void) { int result; return checked_add(INT_MAX - 1, 1, &result) && result == INT_MAX ? 0 : 1; }
