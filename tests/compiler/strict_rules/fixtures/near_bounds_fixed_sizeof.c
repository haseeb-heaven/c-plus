/* ADVERSARIAL near-miss (BOUNDS-FIXED): sizeof-derived last valid index. */
int main(void) { int a[3] = {1, 2, 3}; a[sizeof(a)/sizeof(a[0]) - 1] = 9; return a[2] == 9 ? 0 : 1; }
