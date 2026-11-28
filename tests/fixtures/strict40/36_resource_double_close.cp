#include <stdio.h>
int main(void) { FILE *file = fopen("cplus_resource_test.tmp", "w"); if (!file) return 1; fclose(file); fclose(file); return 0; }
