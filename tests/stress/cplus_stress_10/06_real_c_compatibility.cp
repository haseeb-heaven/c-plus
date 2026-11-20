#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"

#define SQUARE(x) ((x) * (x))

int main(void) {
    int matrix[4][4] = {{0}};
    int sum = 0;
    for (int i = 0; i < 16; i++) {
        matrix[i / 4][i % 4] = i;
    }
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            sum += matrix[row][col];
        }
    }

    FILE *stream = tmpfile();
    if (stream == NULL) return 2;
    const char payload[] = "abc";
    size_t written = fwrite(payload, 1, strlen(payload), stream);
    if (written != 3) { fclose(stream); return 3; }
    rewind(stream);
    char output[4] = {0};
    size_t received = fread(output, 1, 3, stream);
    if (fclose(stream) != 0 || received != 3) return 4;

    print("COMPAT", sum, SQUARE(3), received, output);
    return 0;
}
