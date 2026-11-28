#include <stdio.h>
int main(void){ int*p=NULL; int mode=1; switch(mode){ case 1: { int local_in_case=55; p=&local_in_case; break;} case 2: break;} printf("%d\n",*p); return 0; }
