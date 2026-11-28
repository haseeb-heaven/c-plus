/* Legacy 04 -- FILE* round trip (tmpfile / fwrite / rewind / fread / printf).
 *
 * Writes a text blob to an unnamed temporary stream, rewinds, reads it back
 * into a heap buffer and echoes it. tmpfile() needs no hard-coded path.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    const char *msg = "legacy line one\nlegacy line two\n";
    size_t n = strlen(msg);
    FILE *fp;
    char *buf;
    size_t got;

    fp = tmpfile();
    if (!fp) return 2;

    if (fwrite(msg, 1, n, fp) != n) { fclose(fp); return 3; }
    rewind(fp);

    buf = (char *)malloc(n + 1);
    if (!buf) { fclose(fp); return 2; }
    memset(buf, 0, n + 1);

    got = fread(buf, 1, n, fp);
    buf[got] = '\0';
    printf("stream_roundtrip: got=%lu text=[%s]\n",
           (unsigned long)got, buf);

    free(buf);
    fclose(fp);
    return 0;
}
