/* Regression for issue #192: non-standard %n spellings must still be refused,
 * and scanf_s must accept (not desynchronise on) Annex K rsize_t capacities. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "safe_error.h"
#include "safe_format.h"
#include "safe_io.h"
#include "safe_runtime.h"

int main(void) {
    int sink = 0;
    char out[32];
    char word[16];
    int num = 0;
    FILE *f;
    const char *path = "/tmp/cplus_issue192_scan.tmp";

    cplus_runtime_set_violation_trap(0);

    /* Every spelling of the forbidden %n write is caught before host I/O. */
    errno = 0;
    assert(cplus_printf_s("%In", &sink) == -1 && errno == EINVAL && sink == 0);
    errno = 0;
    assert(cplus_printf_s("%w32n", &sink) == -1 && errno == EINVAL && sink == 0);
    errno = 0;
    assert(cplus_printf_s("%wf32n", &sink) == -1 && errno == EINVAL && sink == 0);
    errno = 0;
    assert(cplus_printf_s("%mn", &sink) == -1 && errno == EINVAL && sink == 0);
    errno = 0;
    assert(cplus_snprintf_s(out, sizeof(out), "%I64n", &sink) == -1 && errno == EINVAL);
    errno = 0;
    assert(cplus_sscanf_s("abc", "%w32n", &sink) == -1 && errno == EINVAL);
    errno = 0;
    assert(cplus_sscanf_s("abc", "%S", word) == -1 && errno == EINVAL);

    /* Annex K shaped scanf: capacity follows the string pointer.  Before the
     * fix vscanf read sizeof(word) as an int* and wrote to address 0x10. */
    memset(word, 0, sizeof(word));
    errno = 0;
    assert(cplus_sscanf_s("abc 42", "%15s %d", word, (size_t)sizeof(word), &num) == 2);
    assert(strcmp(word, "abc") == 0 && num == 42);

    /* Same for fscanf_s, and an unbounded Annex K %s is bounded by capacity. */
    f = cplus_fopen_s(path, "wb+");
    assert(f != NULL);
    assert(fputs("xyz 7", f) >= 0);
    rewind(f);
    memset(word, 0, sizeof(word));
    num = 0;
    assert(cplus_fscanf_s(f, "%15s %d", word, (size_t)sizeof(word), &num) == 2);
    assert(strcmp(word, "xyz") == 0 && num == 7);
    rewind(f);
    memset(word, 0, sizeof(word));
    assert(cplus_fscanf_s(f, "%s %d", word, (size_t)sizeof(word), &num) == 2);
    assert(strcmp(word, "xyz") == 0 && num == 7);
    assert(cplus_fclose_s(f) == 0);

    /* POSIX-shaped calls keep their historical behaviour. */
    num = 0;
    assert(cplus_sscanf_s("99", "%d", &num) == 1 && num == 99);
    assert(cplus_sscanf_s("abc", "%3s", word) == 1 && strcmp(word, "abc") == 0);

    cplus_runtime_set_violation_trap(1);
    unlink(path);
    puts("issue192_format_scan_args: ok");
    return 0;
}
