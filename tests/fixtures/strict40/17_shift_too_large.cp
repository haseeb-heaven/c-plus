#include <limits.h>
int main(void) { volatile int shift = (int)(sizeof(int) * CHAR_BIT); return 1 << shift; }
