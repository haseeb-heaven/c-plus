/* A reserved helper declared in user source is still an interposition. */
extern void cplus_check_bounds(void *ptr, unsigned long n);
int main(void) { cplus_check_bounds(0, 0); return 0; }
