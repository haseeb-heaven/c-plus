/* POSITIVE VLA bounds: loop index provably inside the VLA extent. */
int main(void) { int n = 4; int a[n]; for (int i = 0; i < n; i++) a[i] = i; return a[n - 1] == 3 ? 0 : 1; }
