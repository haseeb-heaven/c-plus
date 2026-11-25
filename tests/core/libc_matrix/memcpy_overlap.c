#include <string.h>
int main(void){char b[8]="abcdefg";memcpy(b+2,b,4);return b[0]==0;}
