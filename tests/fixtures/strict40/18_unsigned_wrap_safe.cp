#include <limits.h>
int main(void) { unsigned int value = UINT_MAX; value = value + 1U; return value != 0U; }
