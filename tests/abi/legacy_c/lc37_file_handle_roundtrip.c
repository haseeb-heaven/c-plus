/*
 * lc37 - file handles: tmpfile / fwrite / rewind / fread round trip.
 *
 * Independent legacy-C ABI probe: binary records written to a temporary
 * FILE* are read back through the same FILE* after rewinding.
 */
#include <stdio.h>
#include <string.h>

struct record {
    int id;
    double value;
};

int main(void)
{
    struct record out[3] = { { 1, 1.5 }, { 2, 2.5 }, { 3, 3.5 } };
    struct record in[3];
    FILE *fp = tmpfile();
    size_t written, read;
    int i, ok = 1;

    if (fp == NULL) {
        printf("lc37 result: FAIL tmpfile\n");
        return 1;
    }
    written = fwrite(out, sizeof(out[0]), 3, fp);
    if (written != 3) {
        printf("lc37 result: FAIL fwrite %zu\n", written);
        (void)fclose(fp);
        return 1;
    }
    rewind(fp);
    read = fread(in, sizeof(in[0]), 3, fp);
    if (read != 3) {
        printf("lc37 result: FAIL fread %zu\n", read);
        (void)fclose(fp);
        return 1;
    }
    for (i = 0; i < 3; ++i) {
        if (in[i].id != out[i].id || in[i].value != out[i].value) {
            ok = 0;
        }
    }
    if (fclose(fp) != 0) {
        ok = 0;
    }
    if (!ok) {
        printf("lc37 result: FAIL data mismatch\n");
        return 1;
    }
    printf("lc37 result: OK records=%zu id3=%d value3=%.1f\n",
           read, in[2].id, in[2].value);
    return 0;
}
