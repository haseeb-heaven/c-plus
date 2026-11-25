#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void *cplus_memcpy(void *restrict dest, const void *restrict src, size_t n);
void *cplus_memmove(void *dest, const void *src, size_t n);
void *cplus_memset(void *s, int c, size_t n);
int cplus_memcmp(const void *s1, const void *s2, size_t n);

char *cplus_strcpy(char *restrict dest, const char *restrict src);
char *cplus_strncpy(char *restrict dest, const char *restrict src, size_t n);
char *cplus_strcat(char *restrict dest, const char *restrict src);
char *cplus_strncat(char *restrict dest, const char *restrict src, size_t n);
int cplus_strcmp(const char *s1, const char *s2);
int cplus_strncmp(const char *s1, const char *s2, size_t n);

FILE *cplus_fopen(const char *path, const char *mode);
int cplus_fclose(FILE *stream);
size_t cplus_fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t cplus_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int cplus_snprintf(char *str, size_t size, const char *fmt, ...);
int cplus_fprintf(FILE *stream, const char *fmt, ...);
int cplus_sscanf(const char *str, const char *fmt, ...);
int cplus_remove(const char *path);
void cplus_exit(int status);
void cplus_abort(void);

int main(void) {
    char buf[16];
    memset(buf, 0, sizeof(buf));

    /* Memory NULL & zero-length checks */
    assert(cplus_memcpy(NULL, NULL, 0) == NULL);
    errno = 0;
    assert(cplus_memcpy(NULL, "a", 1) == NULL && errno == EINVAL);
    errno = 0;
    assert(cplus_memcpy(buf, NULL, 1) == buf && errno == EINVAL);

    errno = 0;
    assert(cplus_memmove(NULL, "a", 1) == NULL && errno == EINVAL);
    assert(cplus_memmove(buf, buf, 4) == buf);

    errno = 0;
    assert(cplus_memset(NULL, 1, 1) == NULL && errno == EINVAL);
    assert(cplus_memset(NULL, 1, 0) == NULL);

    errno = 0;
    assert(cplus_memcmp(NULL, NULL, 0) == 0);
    assert(cplus_memcmp(NULL, "a", 1) < 0 && errno == EINVAL);
    errno = 0;
    assert(cplus_memcmp("a", NULL, 1) > 0 && errno == EINVAL);

    /* String NULL checks */
    errno = 0;
    assert(cplus_strcpy(NULL, "a") == NULL && errno == EINVAL);
    errno = 0;
    assert(cplus_strncpy(buf, NULL, 2) == buf && errno == EINVAL);
    errno = 0;
    assert(cplus_strcat(buf, NULL) == buf && errno == EINVAL);
    errno = 0;
    assert(cplus_strncat(buf, NULL, 2) == buf && errno == EINVAL);
    errno = 0;
    assert(cplus_strcmp(NULL, "a") < 0 && errno == EINVAL);
    errno = 0;
    assert(cplus_strncmp("a", NULL, 1) > 0 && errno == EINVAL);
    assert(cplus_strncmp(NULL, NULL, 0) == 0);

    /* Stdio NULL, overflow, and double-close protection */
    errno = 0;
    assert(cplus_fopen(NULL, "rb") == NULL && errno == EINVAL);
    errno = 0;
    assert(cplus_fopen("/tmp/x", NULL) == NULL && errno == EINVAL);

    const char *tmp_path = "/tmp/cplus_hardened_orig_libc.tmp";
    FILE *fp = cplus_fopen(tmp_path, "wb+");
    assert(fp != NULL);
    errno = 0;
    assert(cplus_fwrite(buf, (size_t)4, SIZE_MAX / 2u, fp) == 0 && errno == EOVERFLOW);
    errno = 0;
    assert(cplus_fread(NULL, 1, 4, fp) == 0 && errno == EINVAL);
    assert(cplus_fclose(fp) == 0);
    /* Double close must be refused cleanly with EOF and EBADF */
    errno = 0;
    assert(cplus_fclose(fp) == EOF && errno == EBADF);
    errno = 0;
    assert(cplus_fclose(NULL) == EOF && errno == EINVAL);
    (void)cplus_remove(tmp_path);

    /* Format & scanf NULL protections */
    errno = 0;
    assert(cplus_snprintf(NULL, 16, "%d", 1) == -1 && errno == EINVAL);
    assert(cplus_snprintf(NULL, 0, "%d", 123) == 3);
    errno = 0;
    assert(cplus_snprintf(buf, sizeof(buf), NULL) == -1 && errno == EINVAL);
    errno = 0;
    assert(cplus_fprintf(NULL, "x") == -1 && errno == EINVAL);
    errno = 0;
    assert(cplus_sscanf(NULL, "%d", buf) == EOF && errno == EINVAL);

    /* Process termination wrappers tested safely in forked children */
    pid_t pid_exit = fork();
    if (pid_exit == 0) {
        cplus_exit(7);
    }
    assert(pid_exit > 0);
    int st_exit = 0;
    assert(waitpid(pid_exit, &st_exit, 0) == pid_exit);
    assert(WIFEXITED(st_exit) && WEXITSTATUS(st_exit) == 7);

    pid_t pid_abort = fork();
    if (pid_abort == 0) {
        cplus_abort();
    }
    assert(pid_abort > 0);
    int st_abort = 0;
    assert(waitpid(pid_abort, &st_abort, 0) == pid_abort);
    assert(WIFSIGNALED(st_abort));

    printf("test_hardened_orig_libc: ok\n");
    return 0;
}
