/* Bug 54: control flow jumping out of a defer cleanup. */
int test(void) { defer { return 99; } return 1; }
int main(void) { return test(); }
