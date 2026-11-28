/* #227: unprototyped K&R declaration states no parameter types. */
extern void evil_kr();
int main(void) { int x = 0; evil_kr(&x); return 0; }
