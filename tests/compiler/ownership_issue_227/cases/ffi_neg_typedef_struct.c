/* #227: canonicalisation must see through the typedef to the record. */
typedef struct { int *p; } BoxT;
extern void evil_typedef(BoxT b);
int main(void) { BoxT b = {0}; evil_typedef(b); return 0; }
