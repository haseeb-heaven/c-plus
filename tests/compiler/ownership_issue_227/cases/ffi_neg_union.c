/* #227: pointer-bearing union passed by value to a bodiless extern. */
union U { int *p; int n; };
extern void evil_union(union U u);
int main(void) { union U u = {0}; evil_union(u); return 0; }
