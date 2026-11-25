#include <stdio.h>
int main(void) { FILE* f = fopen("temp.txt", "w"); if (f) { fclose(f); fprintf(f, "data"); fclose(f); } return 0; }
