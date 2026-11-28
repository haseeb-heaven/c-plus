/* #227: array-of-pointer member inside a by-value struct. */
struct Bag { int *slots[2]; };
extern void evil_array(struct Bag b);
int main(void) { struct Bag b = {0}; evil_array(b); return 0; }
