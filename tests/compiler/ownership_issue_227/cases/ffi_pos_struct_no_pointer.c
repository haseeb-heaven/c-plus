/* Control: a pointer-free by-value struct must not be a false positive. */
struct Pair { int a; double b; };
extern void evil_pair(struct Pair p);
int main(void) { struct Pair p = {0}; evil_pair(p); return 0; }
