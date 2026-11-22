/*
 * Bug CLASS: NULL pointer dereference / NULL handle use.
 *
 * Raw C dereferences NULL (memcpy_s(NULL,...), strcpy(d, NULL), fopen(NULL),
 * fclose(NULL), printf(NULL)). The checked C+ API turns each of these into a
 * defined error return (or a defined non-crashing result) instead of UB.
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_io.h"
#include "safe_format.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_lib_errno.h" /* ESNULLP */

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
        }                                                                      \
    } while (0)

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    char d[8];
    int diff = 99;
    int ind = 0;

    /* --- memory adapters: NULL dest or NULL src --- */
    CHECK(cplus_memcpy_s(NULL, sizeof d, "x", 1) != 0);
    CHECK(cplus_memcpy_s(d, sizeof d, NULL, 1) != 0);
    CHECK(cplus_memmove_s(NULL, sizeof d, "x", 1) != 0);
    CHECK(cplus_memmove_s(d, sizeof d, NULL, 1) != 0);
    CHECK(cplus_memset_s(NULL, sizeof d, 0, 1) != 0);
    CHECK(cplus_memzero_s(NULL, sizeof d) != 0);
    CHECK(cplus_memcmp_s(NULL, sizeof d, "x", 1, &diff) != 0);
    CHECK(cplus_memcmp_s(d, sizeof d, NULL, 1, &diff) != 0);
    CHECK(cplus_memcmp_s(d, sizeof d, "x", 1, NULL) != 0);

    /* --- string adapters: NULL dest or NULL src --- */
    CHECK(cplus_strcpy_s(NULL, sizeof d, "x") != 0);
    CHECK(cplus_strcpy_s(d, sizeof d, NULL) != 0);
    CHECK(cplus_strncpy_s(NULL, sizeof d, "x", 1) != 0);
    CHECK(cplus_strncpy_s(d, sizeof d, NULL, 1) != 0);
    CHECK(cplus_strcat_s(NULL, sizeof d, "x") != 0);
    CHECK(cplus_strcat_s(d, sizeof d, NULL) != 0);
    CHECK(cplus_strncat_s(NULL, sizeof d, "x", 1) != 0);
    CHECK(cplus_strncat_s(d, sizeof d, NULL, 1) != 0);
    CHECK(cplus_strcmp_s(NULL, sizeof d, "x", &ind) != 0);
    CHECK(cplus_strcmp_s(d, sizeof d, NULL, &ind) != 0);
    /* NULL string is a defined result (length 0), never a dereference */
    CHECK(cplus_strnlen_s(NULL, sizeof d) == 0);

    /* --- stdio lifecycle: NULL FILE** / FILE* / name / mode --- */
    FILE *f = NULL;
    CHECK(cplus_fopen_s(NULL, "x", "r") == CPLUS_SAFE_EINVAL);
    CHECK(cplus_fopen_s(&f, NULL, "r") == CPLUS_SAFE_EINVAL);
    CHECK(cplus_fopen_s(&f, "x", NULL) == CPLUS_SAFE_EINVAL);
    CHECK(f == NULL);
    CHECK(cplus_fclose_s(NULL) == CPLUS_SAFE_EINVAL);
    CHECK(cplus_fclose_s(&f) == CPLUS_SAFE_EINVAL); /* f == NULL */

    /* fread_s/fwrite_s with NULL buffer/stream -> defined 0, no deref */
    CHECK(cplus_fread_s(NULL, 4, 1, 1, NULL) == 0);
    CHECK(cplus_fwrite_s(NULL, 4, 1, 1, NULL) == 0);

    /* --- formatting: NULL format string / NULL dest --- */
    errno = 0;
    CHECK(cplus_snprintf_s(d, sizeof d, NULL) == -1);
    CHECK(errno == EINVAL);
    errno = 0;
    CHECK(cplus_snprintf_s(NULL, sizeof d, "%d", 1) == -1);
    CHECK(errno == EINVAL);
    CHECK(cplus_printf_s(NULL) == -1);
    CHECK(cplus_fprintf_s(stdout, NULL) == -1);
    /* dest==NULL with dmax==0 is the defined "size query" form */
    CHECK(cplus_snprintf_s(NULL, 0, "%d", 12345) == 5);

    if (failures == 0) {
        printf("bugclass_null: ok\n");
        return 0;
    }
    printf("bugclass_null: FAILURES=%d\n", failures);
    return 1;
}
