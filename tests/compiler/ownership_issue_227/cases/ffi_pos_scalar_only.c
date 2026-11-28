/* Control: scalar-only externs stay accepted, but are still disclosed. */
extern int evil_scalar(int a, int b);
int main(void) { return evil_scalar(1, 2); }
