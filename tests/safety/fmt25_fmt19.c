#include <stdio.h>
int process(int a){FILE*f=fopen("t","w");if(!f)return -1;if(a)return -1;fclose(f);return 0;}
int main(void){return process(1);}
