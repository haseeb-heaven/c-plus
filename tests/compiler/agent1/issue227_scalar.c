/* Scalar-only externs stay pure by construction; STRICT must accept them. */
extern int ext_add(int a, int b);
int main(void) { return ext_add(1, 2); }
