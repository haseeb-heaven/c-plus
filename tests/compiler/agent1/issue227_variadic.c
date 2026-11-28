/* STRICT must not trust an extern that can receive arbitrary pointer args. */
extern int ext_variadic(int tag, ...);
int main(void) { return ext_variadic(1, 2); }
