/* ADVERSARIAL mutation (ARITH-OVERFLOW): launder the +1 increment through a cast. */
int main(void) { int x = 2147483647; int y = x + (int)1u; return y == 0; }
