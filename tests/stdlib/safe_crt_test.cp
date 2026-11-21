/* Safe CRT tests: overflow, NULL, overlap, truncation, double free, extents. */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/safe_crt/safe_crt.hp"

int main(void) {
    error err = error();
    char buffer[16];
    char *block;
    int value = 0;
    double ratio = 0;
    size_t got;

    void *zero = cplus_malloc_s(0, &err);
    print("malloc-zero", zero == NULL, err.category() == STDLIB_CATEGORY_INVALID);
    block = (char *)cplus_malloc_s(64, &err);
    print("malloc", block != NULL, err.ok());
    cplus_free_s((void **)&block);
    cplus_free_s((void **)&block);                 /* second free is a no-op */
    print("free-noclear", block == NULL);

    void *many = cplus_calloc_s(SIZE_MAX / 2, 8, &err);
    print("calloc-overflow", many == NULL, err.category() == STDLIB_CATEGORY_RANGE);
    void *array = cplus_calloc_s(4, 8, &err);
    print("calloc", array != NULL, ((unsigned char *)array)[0] == 0);

    void *grown = cplus_realloc_s(NULL, 32, &err);
    void *failed = cplus_realloc_s(grown, 0, &err);
    print("realloc-zero", failed == NULL, grown != NULL,
          err.category() == STDLIB_CATEGORY_INVALID);
    cplus_free_s(&grown);

    memset(buffer, 0, sizeof(buffer));
    print("memcpy", cplus_memcpy_s(buffer, sizeof(buffer), "abc", 4, &err) == STDLIB_OK,
          strcmp(buffer, "abc") == 0);
    print("memcpy-overflow",
          cplus_memcpy_s(buffer, 4, "abcdef", 7, &err) == STDLIB_FAIL,
          err.category() == STDLIB_CATEGORY_BOUNDS);
    print("memcpy-null", cplus_memcpy_s(NULL, 8, "x", 2, &err) == STDLIB_FAIL,
          err.category() == STDLIB_CATEGORY_INVALID);

    strcpy(buffer, "abcdef");
    print("memcpy-overlap",
          cplus_memcpy_s(buffer, sizeof(buffer), buffer + 1, 3, &err) == STDLIB_FAIL,
          err.category() == STDLIB_CATEGORY_INVALID);
    print("memmove-overlap",
          cplus_memmove_s(buffer, sizeof(buffer), buffer + 1, 3, &err) == STDLIB_OK,
          buffer[0] == 'b' && buffer[1] == 'c' && buffer[2] == 'd');

    print("strcpy", cplus_strcpy_s(buffer, sizeof(buffer), "hello", &err) == STDLIB_OK,
          strcmp(buffer, "hello") == 0);
    print("strcpy-small",
          cplus_strcpy_s(buffer, 4, "hello", &err) == STDLIB_FAIL,
          buffer[0] == '\0', err.category() == STDLIB_CATEGORY_BOUNDS);
    print("strcpy-null", cplus_strcpy_s(buffer, sizeof(buffer), NULL, &err) == STDLIB_FAIL,
          buffer[0] == '\0');

    cplus_strcpy_s(buffer, sizeof(buffer), "ab", &err);
    print("strcat", cplus_strcat_s(buffer, sizeof(buffer), "cd", &err) == STDLIB_OK,
          strcmp(buffer, "abcd") == 0);
    print("strcat-small", cplus_strcat_s(buffer, 6, "efgh", &err) == STDLIB_FAIL,
          buffer[0] == '\0');

    print("sprintf", cplus_sprintf_s(buffer, sizeof(buffer), &err, "%d-%s", 7,
                                     "ok") > 0,
          strcmp(buffer, "7-ok") == 0);
    print("sprintf-truncated",
          cplus_sprintf_s(buffer, 4, &err, "%s", "abcdef") == STDLIB_FAIL,
          buffer[3] == '\0', err.category() == STDLIB_CATEGORY_BOUNDS);

    FILE *missing = cplus_fopen_s("/nonexistent/safe_crt.txt", "r", &err);
    print("fopen-missing", missing == NULL, err.category() == STDLIB_CATEGORY_IO);
    FILE *stream = cplus_fopen_s("/tmp/cplus_safe_crt.txt", "w+", &err);
    if (stream != NULL) {
        print("fwrite", cplus_fwrite_s("payload", 1, 7, stream, &err) == 7);
        rewind(stream);
        char read_buffer[8];
        got = cplus_fread_s(read_buffer, sizeof(read_buffer), 1, 7, stream, &err);
        print("fread", got == 7, memcmp(read_buffer, "payload", 7) == 0);
        print("fread-extent",
              cplus_fread_s(read_buffer, 4, 1, 7, stream, &err) == 0,
              err.category() == STDLIB_CATEGORY_BOUNDS);
        fclose(stream);
    }

    print("parse-int", cplus_parse_int("42", &value, &err) == STDLIB_OK && value == 42);
    print("parse-int-bad", cplus_parse_int("4x", &value, &err) == STDLIB_FAIL,
          err.category() == STDLIB_CATEGORY_INVALID);
    print("parse-int-overflow",
          cplus_parse_int("99999999999999999999", &value, &err) == STDLIB_FAIL);
    print("parse-double",
          cplus_parse_double("1.5", &ratio, &err) == STDLIB_OK && ratio == 1.5);
    print("parse-double-inf", cplus_parse_double("inf", &ratio, &err) == STDLIB_FAIL);
    return 0;
}
