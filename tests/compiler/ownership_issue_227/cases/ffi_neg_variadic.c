/* #227: variadic tail is untyped -- the pointer travels through '...'. */
extern void evil_variadic(int tag, ...);
int main(void) { int x = 0; evil_variadic(1, &x); return 0; }
