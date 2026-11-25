#include <stdio.h>
int main(void){FILE*f=fopen("/nonexistent_dir/file.txt","r");fprintf(f,"data\n");fclose(f);return 0;}
