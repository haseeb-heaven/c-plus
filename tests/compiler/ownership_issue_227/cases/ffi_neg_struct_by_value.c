/* #227: bodiless extern carrying a pointer inside a by-value struct. */
struct Box { int *p; };
extern void evil_struct(struct Box b);
int main(void) { struct Box b = {0}; evil_struct(b); return 0; }
