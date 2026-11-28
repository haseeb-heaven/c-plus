/* NEGATIVE arith: provable signed overflow in addition. */
int main(void) { int x = 2147483647; int y = x + 1; return y == 0; }
