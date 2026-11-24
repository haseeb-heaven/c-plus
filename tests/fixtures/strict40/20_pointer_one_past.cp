int main(void) { int a[3] = {1,2,3}; int *p = a + 3; return p == a + 3 ? 0 : 1; }
