/* NEGATIVE null: statically known null pointer dereference. */
struct Point { int x; int y; };
int main(void) { struct Point *p = 0; return p->x; }
