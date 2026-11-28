/*
 * lc38 - file handles: fseek / ftell / feof through a tmpfile stream.
 *
 * Independent legacy-C ABI probe: positions are computed with ftell, seek to
 * a record boundary, and verify EOF handling on over-read.
 */
#include <stdio.h>

int main(void)
{
    FILE *fp = tmpfile();
    const unsigned char data[5] = { 10, 20, 30, 40, 50 };
    long size, pos;
    unsigned char value;
    size_t n;
    int extra, c, ok = 1;

    if (fp == NULL) {
        printf("lc38 result: FAIL tmpfile\n");
        return 1;
    }
    if (fwrite(data, 1, 5, fp) != 5) {
        printf("lc38 result: FAIL fwrite\n");
        (void)fclose(fp);
        return 1;
    }
    size = ftell(fp);
    if (fseek(fp, 2L, SEEK_SET) != 0) {
        printf("lc38 result: FAIL fseek\n");
        (void)fclose(fp);
        return 1;
    }
    pos = ftell(fp);
    n = fread(&value, 1, 1, fp);
    if (n != 1 || value != 30u || size != 5L || pos != 2L) {
        ok = 0;
    }
    if (fseek(fp, 0L, SEEK_END) != 0) {
        ok = 0;
    }
    extra = fgetc(fp);
    c = feof(fp) ? 1 : 0;
    if (extra != EOF || c != 1) {
        ok = 0;
    }
    if (fclose(fp) != 0) {
        ok = 0;
    }
    if (!ok) {
        printf("lc38 result: FAIL size=%ld pos=%ld value=%u eof=%d\n",
               size, pos, (unsigned int)value, c);
        return 1;
    }
    printf("lc38 result: OK size=%ld seek=%ld value=%u eof=%d\n",
           size, pos, (unsigned int)value, c);
    return 0;
}
