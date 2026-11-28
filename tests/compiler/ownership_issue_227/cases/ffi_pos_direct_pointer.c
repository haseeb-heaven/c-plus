/* Control: the pre-existing direct-pointer rule is unchanged. */
extern void evil_ptr(int *p);
int main(void) { int x = 0; evil_ptr(&x); return 0; }
