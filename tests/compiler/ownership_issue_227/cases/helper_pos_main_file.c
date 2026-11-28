/* Control: main-file detection of reserved helpers is unchanged. */
long __cplus_checked_mul_i64(long a, long b) { return a * b; }
int main(void) { return (int)__cplus_checked_mul_i64(2, 3) == 6 ? 0 : 1; }
