/* Bug 478 family: fread call whose item count is ignored. */
#include <stdio.h>

int main(void) {
    char buffer[16];
    FILE *stream = fopen("input.bin", "rb");
    if (!stream)
        return 1;
    fread(buffer, 1, sizeof buffer, stream);
    fclose(stream);
    return 0;
}
