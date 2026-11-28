/* Regression for issue #192: non-standard %n spellings are refused by the
 * printf/scanf filters, and the scanf_s family accepts Annex K rsize_t buffer
 * capacities without desynchronising the host vscanf family (historically a
 * SIGSEGV writing through (int *)sizeof(buf)). */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "safe_format.h"
#include "safe_io.h"
#include "safe_log.h"
#include "safe_runtime.h"

int main(void) {
    const char *path = "/tmp/cplus_issue192_scan.tmp";
    int sink = 0, num = 0;
    char out[32], word[16];
    FILE *f;

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_runtime_set_violation_trap(0);

    /* Every %n spelling is caught before host I/O (including glibc 'I'/C23
     * 'wN'/'wfN'/'m' modifiers and POSIX %S unbounded string reads). */
    errno = 0;
    assert(cplus_printf_s("%In", &sink) == -1 && errno == EINVAL && sink == 0);
    errno = 0;
    assert(cplus_printf_s("%w32n", &sink) == -1 && errno == EINVAL && sink == 0);
    errno = 0;
    assert(cplus_snprintf_s(out, sizeof(out), "%wf32n", &sink) == -1 && errno == EINVAL);
    errno = 0;
    assert(cplus_printf_s("%mn", &sink) == -1 && errno == EINVAL && sink == 0);
    errno = 0;
    assert(cplus_snprintf_s(out, sizeof(out), "%I64n", &sink) == -1 && errno == EINVAL);
    errno = 0;
    assert(cplus_sscanf_s("abc", "%w32n", &sink) == -1 && errno == EINVAL);

    /* Annex K shape: an rsize_t capacity follows the buffer pointer. */
    memset(word, 0, sizeof(word));
    errno = 0;
    assert(cplus_sscanf_s("abc 42", "%15s %d", word, (size_t)sizeof(word), &num) == 2);
    assert(strcmp(word, "abc") == 0 && num == 42);

    /* Same for fscanf_s and with an unbounded %s bounded by the capacity. */
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
    num = 0;
    assert(cplus_fscanf_s(f, "%15s %d", word, (size_t)sizeof(word), &num) == 2);
    assert(strcmp(word, "xyz") == 0 && num == 7);
    assert(cplus_fclose_s(f) == 0);

    /* POSIX-shaped calls and bounded conversions keep their behaviour. */
    num = 0;
    assert(cplus_sscanf_s("99", "%d", &num) == 1 && num == 99);
    assert(cplus_sscanf_s("abc", "%3s", word) == 1 && strcmp(word, "abc") == 0);
    errno = 0;
    assert(cplus_sscanf_s("abc", "%S", word) == -1 && errno == EINVAL);

    cplus_runtime_set_violation_trap(1);
    unlink(path);
    puts("issue192_format_scan_args: ok");
    return 0;
}
