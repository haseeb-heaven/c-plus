int divide_values(int a, int b, int *out) { if (b == 0 || out == 0) return 0; *out = a / b; return 1; }
int main(void) { int result = 0; return divide_values(10, 2, &result) && result == 5 ? 0 : 1; }
