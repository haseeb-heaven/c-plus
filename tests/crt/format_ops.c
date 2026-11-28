/* CRT conformance: snprintf and scanf conversion/error paths. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int condition, const char *what) {
    if (!condition) {
        failures++;
        printf("FAIL %s\n", what);
    }
}

int main(void) {
    char buffer[32];
    int value = 0;
    double ratio = 0;

    check(snprintf(buffer, sizeof(buffer), "%d", 42) == 2, "snprintf int");
    check(strcmp(buffer, "42") == 0, "snprintf content");
    check(snprintf(buffer, sizeof(buffer), "%s", "a longer string") == 15,
          "snprintf string length");
    check(snprintf(NULL, 0, "%d", 7) == 1, "snprintf sizing call");

    /* scanf from a memory stream: conversions, partial input and EOF.
     * fmemopen takes a writable void * buffer, so use mutable copies of the
     * input text (same contents and same size as the former literals). */
    char input[] = "42 2.5 trailing";
    FILE *stream = fmemopen(input, 15, "r");
    if (stream == NULL) {
        printf("format_ops: fmemopen unavailable\n");
        return 0;
    }
    check(fscanf(stream, "%d", &value) == 1 && value == 42, "fscanf int");
    check(fscanf(stream, "%lf", &ratio) == 1 && ratio > 2.4 && ratio < 2.6,
          "fscanf double");
    char word[16];
    check(fscanf(stream, "%15s", word) == 1 && strcmp(word, "trailing") == 0,
          "fscanf string");
    int at_end = fscanf(stream, "%d", &value);
    check(at_end == EOF || at_end == 0, "fscanf at end of input");
    fclose(stream);

    /* A failed conversion returns 0, not EOF, and leaves the stream usable. */
    char bad_input[] = "abc";
    FILE *bad = fmemopen(bad_input, 3, "r");
    if (bad != NULL) {
        check(fscanf(bad, "%d", &value) == 0, "fscanf invalid -> 0");
        check(fscanf(bad, "%15s", word) == 1, "stream still usable after failure");
        fclose(bad);
    }

    printf("format_ops: %s\n", failures == 0 ? "ok" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
