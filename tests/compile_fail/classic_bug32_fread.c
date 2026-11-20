/* Bug 32: unchecked fread. */
#include <stdio.h>
int main(void) {
    FILE *fp = fopen("small.bin", "rb");
    if (!fp) return 1;
    int values[10];
    fread(values, sizeof(int), 10, fp);
    printf("%d\n", values[9]);
    fclose(fp);
    return 0;
}
