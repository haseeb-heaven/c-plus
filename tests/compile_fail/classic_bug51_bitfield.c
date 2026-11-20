/* Bug 51: non-portable bit-field layout/signedness. */
#include <stdint.h>
#include <stdio.h>
struct Flags { int flag:1; uint8_t mode:3; };
int main(void) { struct Flags f={.flag=1,.mode=5}; printf("%d\n",f.flag); return 0; }
