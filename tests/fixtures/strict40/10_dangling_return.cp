int *bad_pointer(void) { int value = 42; return &value; }
int main(void) { int *p = bad_pointer(); return *p; }
