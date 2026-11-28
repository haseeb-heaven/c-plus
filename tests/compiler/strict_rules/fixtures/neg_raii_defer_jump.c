/* NEGATIVE RAII: control flow jumps out of a defer cleanup. */
int test(void) { defer { return 99; } return 1; }
int main(void) { return test(); }
