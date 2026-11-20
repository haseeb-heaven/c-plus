#include <stdio.h>
static void kind(char c) {
    switch (c) {
        case '0' ... '9': printf("digit\n"); break;
        case 'a' ... 'z':
        case 'A' ... 'Z': printf("letter\n"); break;
        default: printf("other\n"); break;
    }
}
int main(void) { kind('7'); kind('Q'); kind('!'); return 0; }
