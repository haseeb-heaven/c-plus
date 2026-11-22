#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "safe_alloc.h"
#include "safe_memory.h"
#include "safe_runtime.h"
#include "safe_string.h"

static int guarded_uaf_body(void *ctx) {
    int *p = (int *)ctx;
    memset_s(p, 0, sizeof(int) * 4);
    return 0;
}

int main(void) {
    assert(cplus_runtime_get_violation_trap() == 1);

    /* 0a. Default unguarded UAF fails stop with exit 70 (never reaches _exit(0)) */
    pid_t pid_uaf = fork();
    if (pid_uaf == 0) {
        int *p = (int *)malloc_s(sizeof(int) * 4);
        free_s(p);
        memset_s(p, 0, sizeof(int) * 4);
        _exit(0);
    }
    assert(pid_uaf > 0);
    int st_uaf = 0;
    assert(waitpid(pid_uaf, &st_uaf, 0) == pid_uaf);
    assert(WIFEXITED(st_uaf) && WEXITSTATUS(st_uaf) == 70);

    /* 0b. Default unguarded heap overflow fails stop with exit 70 */
    pid_t pid_ovf = fork();
    if (pid_ovf == 0) {
        int *t = (int *)malloc_s(1);
        memset_s(t, 0, 20);
        _exit(0);
    }
    assert(pid_ovf > 0);
    int st_ovf = 0;
    assert(waitpid(pid_ovf, &st_ovf, 0) == pid_ovf);
    assert(WIFEXITED(st_ovf) && WEXITSTATUS(st_ovf) == 70);

    /* 0c. Inside cplus_runtime_guard, UAF unwinds cleanly without aborting */
    int *gp = (int *)malloc_s(sizeof(int) * 4);
    assert(gp != NULL);
    free_s(gp);
    assert(cplus_runtime_guard(guarded_uaf_body, gp) == CPLUS_RUNTIME_ERROR);

    /* Disable fatal violation trapping to test recoverable NULL/ERANGE/EFAULT contracts */
    cplus_runtime_set_violation_trap(0);
    assert(cplus_runtime_get_violation_trap() == 0);

    /* 1. Too-small heap allocation caught by 3-arg memset_s / memcpy_s */
    int *tiny = (int *)malloc_s(1);
    assert(tiny != NULL);
    errno = 0;
    assert(memset_s(tiny, 0, sizeof(int) * 5) == NULL && errno == ERANGE);
    int initial[] = {10, 40, 50, 60};
    errno = 0;
    assert(memcpy_s(tiny, initial, sizeof(initial)) == NULL && errno == ERANGE);
    free_s(tiny);

    /* 2. Valid heap allocation succeeds with 3-arg memory_s */
    int *arr = (int *)malloc_s(sizeof(int) * 5);
    assert(arr != NULL);
    assert(memset_s(arr, 0, sizeof(int) * 5) == arr);
    assert(memcpy_s(arr, initial, sizeof(initial)) == arr);
    assert(arr[0] == 10 && arr[1] == 40 && arr[2] == 50 && arr[3] == 60 && arr[4] == 0);
    assert(memmove_s(arr + 1, arr, sizeof(int) * 3) == arr + 1);
    assert(arr[1] == 10 && arr[2] == 40 && arr[3] == 50);
    assert(memcmp_s(arr, arr, sizeof(int)) == 0);
    free_s(arr);

    /* 3. Stack buffer checks via __builtin_object_size and string_s auto wrappers */
    char buf[4];
    errno = 0;
    assert(memcpy_s(buf, "abcdef", 6) == NULL && errno == ERANGE);
    errno = 0;
    assert(strcpy_s(buf, "abcdef") == NULL && errno == ERANGE);
    assert(strcpy_s(buf, "ab") == buf);
    assert(strcmp(buf, "ab") == 0);
    errno = 0;
    assert(strcat_s(buf, "xyz") == NULL && errno == ERANGE);
    assert(strcpy_s(buf, "ab") == buf);
    assert(strcat_s(buf, "c") == buf);
    assert(strcmp(buf, "abc") == 0);
    assert(strncpy_s(buf, "xy", 2) == buf);
    assert(strcmp(buf, "xy") == 0);
    assert(strncat_s(buf, "z", 1) == buf);
    assert(strcmp(buf, "xyz") == 0);

    /* 4. Standard Annex K 4-arg / 5-arg forms remain supported */
    assert(memset_s(buf, sizeof(buf), 0, sizeof(buf)) == 0);
    assert(memcpy_s(buf, sizeof(buf), "ok", 3) == 0);
    int diff = -1;
    assert(memcmp_s(buf, sizeof(buf), "ok", 3, &diff) == 0 && diff == 0);

    /* 5. NULL & zero-length checks across auto wrappers */
    errno = 0; assert(memcpy_s(NULL, buf, 1) == NULL && errno == EINVAL);
    errno = 0; assert(memcpy_s(NULL, buf, 0) == NULL && errno == EINVAL);
    assert(memcpy_s(buf, buf, 0) == buf);
    errno = 0; assert(memmove_s(NULL, buf, 1) == NULL && errno == EINVAL);
    errno = 0; assert(memmove_s(NULL, buf, 0) == NULL && errno == EINVAL);
    assert(memmove_s(buf, buf, 0) == buf);
    errno = 0; assert(memset_s(NULL, 0, 1) == NULL && errno == EINVAL);
    assert(memset_s(buf, 0, 0) == buf);
    assert(memcmp_s(NULL, NULL, 0) == 0);
    errno = 0; assert(memcmp_s(NULL, buf, 1) < 0 && errno == EINVAL);
    errno = 0; assert(memcmp_s(buf, NULL, 1) > 0 && errno == EINVAL);
    assert(memcmp_s(buf, "ok", 100) == -1 && errno == ERANGE);

    errno = 0; assert(strcpy_s(NULL, "a") == NULL && errno == EINVAL);
    errno = 0; assert(strncpy_s(NULL, "a", 1) == NULL && errno == EINVAL);
    assert(strncpy_s(buf, "a", 0) == buf);
    errno = 0; assert(strncpy_s(buf, "abcdef", 5) == NULL && errno == ERANGE);
    errno = 0; assert(strcat_s(NULL, "a") == NULL && errno == EINVAL);
    errno = 0; assert(strncat_s(NULL, "a", 1) == NULL && errno == EINVAL);
    assert(strncat_s(buf, "a", 0) == buf);
    errno = 0; assert(strncat_s(buf, "abcd", 4) == NULL && errno == ERANGE);

    /* 6. Use-after-free refusal across memory_s and string_s */
    int *freed = (int *)malloc_s(sizeof(int) * 4);
    assert(freed != NULL);
    free_s(freed);
    assert(cplus_safe_ptr_was_freed(freed) == 1);
    errno = 0; assert(memset_s(freed, 0, sizeof(int) * 4) == NULL && errno == EFAULT);
    errno = 0; assert(memcpy_s(freed, initial, sizeof(int) * 4) == NULL && errno == EFAULT);
    errno = 0; assert(memmove_s(freed, initial, sizeof(int) * 4) == NULL && errno == EFAULT);
    errno = 0; assert(memcmp_s(freed, initial, sizeof(int)) == -1 && errno == EFAULT);
    errno = 0; assert(cplus_memset_s(freed, 16, 0, 16) == 400 && errno == EFAULT);
    errno = 0; assert(cplus_memcpy_s(freed, 16, initial, 16) == 400 && errno == EFAULT);
    errno = 0; assert(cplus_memmove_s(freed, 16, initial, 16) == 400 && errno == EFAULT);
    errno = 0; assert(cplus_memcmp_s(freed, 16, initial, 16, &diff) == 400 && errno == EFAULT);
    errno = 0; assert(memzero_s(freed, 16) == 400 && errno == EFAULT);

    char *sfreed = (char *)malloc_s(16);
    assert(sfreed != NULL);
    free_s(sfreed);
    errno = 0; assert(strcpy_s(sfreed, "hi") == NULL && errno == EFAULT);
    errno = 0; assert(strncpy_s(sfreed, "hi", 2) == NULL && errno == EFAULT);
    errno = 0; assert(strcat_s(sfreed, "hi") == NULL && errno == EFAULT);
    errno = 0; assert(strncat_s(sfreed, "hi", 2) == NULL && errno == EFAULT);
    errno = 0; assert(cplus_strcpy_s(sfreed, 16, "hi") == 400 && errno == EFAULT);
    errno = 0; assert(cplus_strncpy_s(sfreed, 16, "hi", 2) == 400 && errno == EFAULT);
    errno = 0; assert(cplus_strcat_s(sfreed, 16, "hi") == 400 && errno == EFAULT);
    errno = 0; assert(cplus_strncat_s(sfreed, 16, "hi", 2) == 400 && errno == EFAULT);
    int ind = 9;
    errno = 0; assert(cplus_strcmp_s(sfreed, 16, "hi", &ind) == 400 && errno == EFAULT);
    errno = 0; assert(strnlen_s(sfreed, 16) == 0 && errno == EFAULT);
    size_t tmax = 16;
    char *tptr = NULL;
    errno = 0; assert(strtok_s(sfreed, &tmax, ",", &tptr) == NULL && errno == EFAULT);
    errno = 0; assert(strerror_s(sfreed, 16, EINVAL) == 400 && errno == EFAULT);

    cplus_runtime_set_violation_trap(1);
    assert(cplus_runtime_get_violation_trap() == 1);

    printf("test_auto_bounds_s: ok\n");
    return 0;
}
