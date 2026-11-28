#include <stdio.h>
#include <string.h>
int main(void) { char buf[5]; memcpy(buf, "hello", 5); size_t len = strlen(buf); printf("%zu\n", len); return 0; }
