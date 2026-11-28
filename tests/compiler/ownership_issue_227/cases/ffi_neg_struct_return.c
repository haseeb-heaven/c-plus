/* #227: bodiless extern returning a struct that contains a pointer. */
struct Box { int *p; };
extern struct Box evil_return(void);
int main(void) { struct Box b = evil_return(); return b.p != 0; }
