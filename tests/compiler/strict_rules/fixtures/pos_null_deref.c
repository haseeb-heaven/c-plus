/* POSITIVE null: callee checks the pointer before dereferencing. */
static int deref(int *p) { if (!p) return -1; return *p; }
int main(void) { return deref(0) == -1 ? 0 : 1; }
