#include <stdio.h>
int main(void){ goto skip_init; int*ptr=NULL; skip_init: if(ptr!=NULL){*ptr=5;} return 0; }
