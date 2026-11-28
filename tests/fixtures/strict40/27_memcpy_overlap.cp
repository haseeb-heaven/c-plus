#include <string.h>
int main(void) { char text[] = "abcdef"; memcpy(text + 1, text, 5); return 0; }
