#include <string.h>
int main(void) { char text[] = "abcdef"; memmove(text + 1, text, 5); return text[1] == 'a' ? 0 : 1; }
