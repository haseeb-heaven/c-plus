/* ADVERSARIAL near-miss (ARITH-OVERFLOW): largest non-overflowing addition. */
int main(void) { int x = 2147483646; int y = x + 1; return y == 2147483647 ? 0 : 1; }
