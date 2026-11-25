#include <stdio.h>
int main(void) { FILE* f = fopen("missing_file_xyz.txt", "r"); fclose(f); return 0; }
