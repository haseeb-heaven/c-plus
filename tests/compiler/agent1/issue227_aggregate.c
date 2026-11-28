/* An aggregate parameter can carry a pointer; STRICT must not trust it. */
struct P { int *ptr; };
extern void ext_struct(struct P p);
int main(void) { struct P p; p.ptr = 0; ext_struct(p); return 0; }
