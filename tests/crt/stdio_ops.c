/* CRT conformance: fopen/fwrite/fread/fseek/ftell/fflush/fclose and EOF. */
#include <stdio.h>
#include <string.h>

static int failures;

static void check(int condition, const char *what) {
    if (!condition) {
        failures++;
        printf("FAIL %s\n", what);
    }
}

int main(void) {
    const char *path = "/tmp/cplus_crt_stdio.bin";
    unsigned char payload[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
                                 15, 16};

    FILE *out = fopen(path, "wb");
    check(out != NULL, "fopen write");
    if (out == NULL)
        return 1;
    check(fwrite(payload, 1, sizeof(payload), out) == sizeof(payload),
          "fwrite full");
    check(fflush(out) == 0, "fflush");
    check(fclose(out) == 0, "fclose writer");

    FILE *in = fopen(path, "rb");
    check(in != NULL, "fopen read");
    if (in == NULL)
        return 1;
    unsigned char read_back[32];
    check(fread(read_back, 1, sizeof(read_back), in) == sizeof(payload),
          "fread stops at EOF");
    check(memcmp(read_back, payload, sizeof(payload)) == 0, "fread content");
    check(ferror(in) == 0, "no error at EOF");
    check(fread(read_back, 1, 1, in) == 0, "read at EOF returns 0");
    check(feof(in) != 0, "feof set");

    check(fseek(in, 4, SEEK_SET) == 0, "fseek");
    check(ftell(in) == 4, "ftell after seek");
    check(fread(read_back, 1, 1, in) == 1 && read_back[0] == 5, "seeked read");
    check(fclose(in) == 0, "fclose reader");

    check(fopen("/nonexistent/cplus/crt/file", "rb") == NULL,
          "fopen missing -> NULL");
    remove(path);
    printf("stdio_ops: %s\n", failures == 0 ? "ok" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
