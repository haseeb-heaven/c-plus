/* Bug 479 family: fwrite call whose item count is ignored. */
#include <stdio.h>

int main(void) {
    char payload[16] = {0};
    FILE *stream = fopen("output.bin", "wb");
    if (!stream)
        return 1;
    fwrite(payload, 1, sizeof payload, stream);
    fclose(stream);
    return 0;
}
