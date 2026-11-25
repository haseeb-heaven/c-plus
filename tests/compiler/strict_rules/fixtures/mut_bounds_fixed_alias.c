/* ADVERSARIAL mutation (BOUNDS-FIXED): reach past 'a' through an alias pointer. */
int main(void) { int a[3] = {1, 2, 3}; int *p = a; p[5] = 9; return 0; }
