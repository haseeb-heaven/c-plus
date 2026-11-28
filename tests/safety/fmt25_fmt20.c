#include <stdio.h>
int main(void){FILE*f=fopen("t","w");if(f){FILE*f2=f;fclose(f);f=NULL;fprintf(f2,"use after close\n");}return 0;}
