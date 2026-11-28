int main(void) { int a[4] = {0}; int *p = a + 4; return p == a + 4 ? 0 : 1; }
