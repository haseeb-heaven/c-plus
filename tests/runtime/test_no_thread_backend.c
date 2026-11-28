/*===-- test_no_thread_backend.c - fail-loud fallback regression ------------===
 *
 * Compiled with -DCPLUS_FORCE_NO_THREAD_BACKEND so the runtime takes its
 * "no thread backend" branch even on a POSIX host.  Every exported mutex/TLS
 * primitive must terminate with CPLUS_EXIT_UNSUPPORTED and a clear diagnostic;
 * none may silently return NULL or accept a call as a successful no-op
 * (issue #75).
 *
 *===----------------------------------------------------------------------===*/

#include "cplus/cplus.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures = 0;

static void call_mutex_new(void) { (void)cplus_mutex_new(); }
static void call_mutex_lock(void) { cplus_mutex_lock((void *)(uintptr_t)0x1); }
static void call_mutex_unlock(void) {
  cplus_mutex_unlock((void *)(uintptr_t)0x1);
}
static void call_mutex_free(void) { cplus_mutex_free((void *)(uintptr_t)0x1); }
static void call_tls_new(void) { (void)cplus_tls_new(); }
static void call_tls_get(void) { (void)cplus_tls_get((void *)(uintptr_t)0x1); }
static void call_tls_set(void) {
  cplus_tls_set((void *)(uintptr_t)0x1, (void *)(uintptr_t)0x2);
}
static void call_tls_free(void) { cplus_tls_free((void *)(uintptr_t)0x1); }

static void expect_fatal(void (*fn)(void), const char *name) {
  char path[192];
  snprintf(path, sizeof(path), "build/runtime/no_backend_%s.err", name);

  fflush(stdout);
  pid_t pid = fork();
  if (pid < 0) {
    fprintf(stderr, "  FAIL %s: fork failed\n", name);
    ++failures;
    return;
  }
  if (pid == 0) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
      dup2(fd, STDERR_FILENO);
    fn();
    _Exit(0); /* must be unreachable */
  }
  int status = 0;
  if (waitpid(pid, &status, 0) != pid) {
    fprintf(stderr, "  FAIL %s: waitpid failed\n", name);
    ++failures;
    return;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != CPLUS_EXIT_UNSUPPORTED) {
    fprintf(stderr, "  FAIL %s: expected exit %d, got status 0x%x\n", name,
            CPLUS_EXIT_UNSUPPORTED, status);
    ++failures;
    return;
  }
  FILE *f = fopen(path, "r");
  char buf[512] = {0};
  if (f) {
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
  }
  if (strstr(buf, "unsupported thread backend") == NULL) {
    fprintf(stderr, "  FAIL %s: missing fail-loud diagnostic\n", name);
    ++failures;
  }
}

int main(void) {
  expect_fatal(call_mutex_new, "mutex_new");
  expect_fatal(call_mutex_lock, "mutex_lock");
  expect_fatal(call_mutex_unlock, "mutex_unlock");
  expect_fatal(call_mutex_free, "mutex_free");
  expect_fatal(call_tls_new, "tls_new");
  expect_fatal(call_tls_get, "tls_get");
  expect_fatal(call_tls_set, "tls_set");
  expect_fatal(call_tls_free, "tls_free");

  if (failures == 0) {
    printf("no_thread_backend: ok\n");
    return 0;
  }
  printf("no_thread_backend: %d check(s) failed\n", failures);
  return 1;
}
