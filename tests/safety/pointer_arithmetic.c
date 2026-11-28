int main(void) { int a[4] = {0}; int *p = a + 8; return *p; }  // NOSONAR intentional safety regression probe
