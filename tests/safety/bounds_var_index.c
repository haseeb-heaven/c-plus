int main(void) { int a[4] = {0}; int i = 5; a[i] = 1; return a[i]; }  // NOSONAR intentional unsafe fixture for the C+ safe-mode checker
