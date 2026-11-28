/*===-- harness.c - API coverage scenario harness ----------------*- C -*-===*
 *
 * Hand-written support code for the generated API-coverage scenarios.
 * No API under test is mocked: every scenario calls the real compiler/runtime
 * exported from runtime/include/cplus/cplus.h and runtime/cplus_runtime.c.
 *
 *===----------------------------------------------------------------------===*/
#include "harness.h"

#include "cplus/cplus.h"

#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Provided by libgcov when the binary is built with -fprofile-arcs.
 * Calling it in a trapped child flushes that child's counters to the same
 * .gcda used by the parent, so libgcov merges the fatal-path counters when
 * the parent exits normally. */
extern void __gcov_dump(void);

char g_detail[512];
volatile int g_drop_calls = 0;

void h_drop_counter_reset(void) { g_drop_calls = 0; }
void h_drop_callback(void *self) {
  (void)self;
  ++g_drop_calls;
}

static int set_i64(int64_t got, int64_t want, const char *what) {
  if (got == want)
    return 1;
  snprintf(g_detail, sizeof(g_detail), "%s: got %lld, expected %lld", what,
           (long long)got, (long long)want);
  return 0;
}

int h_eq_i64(int64_t got, int64_t want, const char *what) {
  return set_i64(got, want, what);
}

int h_eq_u64(uint64_t got, uint64_t want, const char *what) {
  if (got == want)
    return 1;
  snprintf(g_detail, sizeof(g_detail), "%s: got 0x%llx, expected 0x%llx", what,
           (unsigned long long)got, (unsigned long long)want);
  return 0;
}

int h_eq_sz(size_t got, size_t want, const char *what) {
  if (got == want)
    return 1;
  snprintf(g_detail, sizeof(g_detail), "%s: got %zu, expected %zu", what, got,
           want);
  return 0;
}

int h_eq_str(const char *got, const char *want, const char *what) {
  if (got && want && strcmp(got, want) == 0)
    return 1;
  snprintf(g_detail, sizeof(g_detail), "%s: got \"%s\", expected \"%s\"", what,
           got ? got : "(null)", want ? want : "(null)");
  return 0;
}

int h_has_substr(const char *haystack, const char *needle, const char *what) {
  if (needle && haystack && strstr(haystack, needle))
    return 1;
  snprintf(g_detail, sizeof(g_detail), "%s: output does not contain \"%s\" "
           "(output was \"%s\")", what, needle ? needle : "(null)",
           haystack ? haystack : "(null)");
  return 0;
}

/* ---------------------------- stdout capture ---------------------------- */

static int g_saved_out = -1;
static char g_out_path[] = "/tmp/cplus_api_cov_out_XXXXXX";
static char g_out_buf[32768];

void h_capture_begin(void) {
  fflush(stdout);
  g_saved_out = dup(1);
  int fd = mkstemp(g_out_path);
  if (fd < 0 || g_saved_out < 0) {
    fprintf(stderr, "harness: stdout capture setup failed: %s\n",
            strerror(errno));
    _Exit(97);
  }
  if (dup2(fd, 1) < 0) {
    fprintf(stderr, "harness: dup2(stdout) failed: %s\n", strerror(errno));
    _Exit(97);
  }
  close(fd);
}

const char *h_capture_end(void) {
  fflush(stdout);
  if (g_saved_out >= 0) {
    if (dup2(g_saved_out, 1) < 0) {
      fprintf(stderr, "harness: dup2 restore failed: %s\n", strerror(errno));
      _Exit(97);
    }
    close(g_saved_out);
    g_saved_out = -1;
  }
  g_out_buf[0] = '\0';
  int fd = open(g_out_path, O_RDONLY);
  if (fd >= 0) {
    ssize_t n = read(fd, g_out_buf, sizeof(g_out_buf) - 1);
    if (n > 0)
      g_out_buf[n] = '\0';
    close(fd);
  }
  unlink(g_out_path);
  return g_out_buf;
}

/* ----------------------------- stdin injection -------------------------- */

static int g_saved_in = -1;
static char g_in_path[] = "/tmp/cplus_api_cov_in_XXXXXX";

void h_stdin_from(const char *text) {
  g_saved_in = dup(0);
  int fd = mkstemp(g_in_path);
  if (fd < 0 || g_saved_in < 0) {
    fprintf(stderr, "harness: stdin setup failed: %s\n", strerror(errno));
    _Exit(97);
  }
  size_t n = strlen(text);
  if (write(fd, text, n) != (ssize_t)n) {
    fprintf(stderr, "harness: stdin write failed: %s\n", strerror(errno));
    _Exit(97);
  }
  if (lseek(fd, 0, SEEK_SET) == (off_t)-1 || dup2(fd, 0) < 0) {
    fprintf(stderr, "harness: stdin dup2 failed: %s\n", strerror(errno));
    _Exit(97);
  }
  close(fd);
  clearerr(stdin);
  if (fseek(stdin, 0, SEEK_SET) != 0)
    clearerr(stdin);
}

void h_stdin_restore(void) {
  if (g_saved_in >= 0) {
    fflush(stdin);
    if (dup2(g_saved_in, 0) < 0) {
      fprintf(stderr, "harness: stdin restore failed: %s\n", strerror(errno));
      _Exit(97);
    }
    close(g_saved_in);
    g_saved_in = -1;
  }
  clearerr(stdin);
  unlink(g_in_path);
}

/* ------------------------- forked trap scenarios ------------------------ */

/* ---------------- child-side gcov flushing for trap scenarios ----------- */

/* cplus_print_fatal() invokes this hook immediately before _Exit(code). */
static void child_fatal_gcov_hook(cplus_exit_code code, const char *kind) {
  (void)code;
  (void)kind;
  __gcov_dump();
}

/* abort() paths (notably __cplus_bounds_check) never reach the fatal hook.
 * Dump from the signal handler, then restore SIGABRT's default disposition and
 * re-raise so the child still dies with the exact signal the suite asserts. */
static void child_abort_gcov_handler(int sig) {
  (void)sig;
  __gcov_dump();
  struct sigaction dfl;
  memset(&dfl, 0, sizeof(dfl));
  dfl.sa_handler = SIG_DFL;
  sigemptyset(&dfl.sa_mask);
  sigaction(SIGABRT, &dfl, NULL);
  raise(SIGABRT);
  _Exit(128 + SIGABRT); /* defensive: raise() must not return */
}

static void install_child_gcov_flush(void) {
  cplus_fatal_hook = child_fatal_gcov_hook;
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = child_abort_gcov_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  (void)sigaction(SIGABRT, &sa, NULL);
}

static double now_seconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int h_fork_trap(void (*fn)(void), int expect_status,
                const char *expect_substr) {
  char path[] = "/tmp/cplus_api_cov_err_XXXXXX";
  int errfd = mkstemp(path);
  if (errfd < 0) {
    snprintf(g_detail, sizeof(g_detail), "mkstemp failed: %s", strerror(errno));
    return 0;
  }
  fflush(NULL);
  pid_t pid = fork();
  if (pid < 0) {
    snprintf(g_detail, sizeof(g_detail), "fork failed: %s", strerror(errno));
    close(errfd);
    unlink(path);
    return 0;
  }
  if (pid == 0) {
    if (dup2(errfd, 2) < 0)
      _Exit(96);
    close(errfd);
    /* Flush gcov counters before _Exit/abort so fatal-path lines are merged
     * into the parent's .gcda instead of being lost with the child. */
    install_child_gcov_flush();
    fn();
    /* Reaching here means the expected trap did not happen. */
    fputs("HARNESS_NO_TRAP", stderr);
    fflush(stderr);
    __gcov_dump();
    _Exit(222);
  }
  close(errfd);

  /* Hard 5-second timeout: poll waitpid, then SIGKILL and reap. */
  double deadline = now_seconds() + 5.0;
  int status = 0;
  int reaped = 0;
  for (;;) {
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) {
      reaped = 1;
      break;
    }
    if (r < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (now_seconds() >= deadline) {
      kill(pid, SIGKILL);
      (void)waitpid(pid, &status, 0);
      snprintf(g_detail, sizeof(g_detail), "TIMEOUT after 5s waiting for trap");
      unlink(path);
      return 0;
    }
    struct timespec nap = {0, 2000000}; /* 2 ms */
    nanosleep(&nap, NULL);
  }
  if (!reaped) {
    snprintf(g_detail, sizeof(g_detail), "waitpid failed");
    unlink(path);
    return 0;
  }

  int got_status;
  if (WIFEXITED(status))
    got_status = WEXITSTATUS(status);
  else if (WIFSIGNALED(status))
    got_status = 128 + WTERMSIG(status);
  else
    got_status = -1;

  char buf[4096];
  buf[0] = '\0';
  int fd = open(path, O_RDONLY);
  if (fd >= 0) {
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0)
      buf[n] = '\0';
    close(fd);
  }
  unlink(path);

  if (got_status != expect_status) {
    snprintf(g_detail, sizeof(g_detail),
             "child status: got %d, expected %d; stderr=\"%s\"", got_status,
             expect_status, buf);
    return 0;
  }
  if (expect_substr && !strstr(buf, expect_substr)) {
    snprintf(g_detail, sizeof(g_detail),
             "stderr missing \"%s\": got \"%s\"", expect_substr, buf);
    return 0;
  }
  return 1;
}

int h_fork_trap_stdin(void (*fn)(void), int expect_status,
                      const char *expect_substr, const char *input) {
  h_stdin_from(input);
  int rc = h_fork_trap(fn, expect_status, expect_substr);
  h_stdin_restore();
  return rc;
}

/* --------------------- in-process hook trap scenarios ------------------- */

static jmp_buf g_hook_jmp;
static int g_hook_jmp_ready = 0;
static int g_hook_code = -1;
static const char *g_hook_kind = NULL;
static void (*g_old_hook)(cplus_exit_code, const char *) = NULL;

static void capture_hook(cplus_exit_code code, const char *kind) {
  g_hook_code = (int)code;
  g_hook_kind = kind;
  if (g_hook_jmp_ready) {
    g_hook_jmp_ready = 0;
    longjmp(g_hook_jmp, 1);
  }
}

int h_hook_trap(void (*fn)(void), int expect_code, const char *expect_kind,
                const char *expect_substr) {
  char path[] = "/tmp/cplus_api_cov_hook_XXXXXX";
  int errfd = mkstemp(path);
  if (errfd < 0) {
    snprintf(g_detail, sizeof(g_detail), "mkstemp failed: %s", strerror(errno));
    return 0;
  }
  int saved_err = dup(2);
  if (saved_err < 0 || dup2(errfd, 2) < 0) {
    snprintf(g_detail, sizeof(g_detail), "stderr capture setup failed");
    close(errfd);
    if (saved_err >= 0)
      close(saved_err);
    unlink(path);
    return 0;
  }
  close(errfd);

  g_old_hook = cplus_fatal_hook;
  g_hook_code = -1;
  g_hook_kind = NULL;
  cplus_fatal_hook = capture_hook;

  int trapped = 0;
  if (setjmp(g_hook_jmp) == 0) {
    g_hook_jmp_ready = 1;
    fn();
    g_hook_jmp_ready = 0;
  } else {
    trapped = 1;
  }

  cplus_fatal_hook = g_old_hook;
  fflush(stderr);
  if (dup2(saved_err, 2) < 0)
    _Exit(95);
  close(saved_err);

  char buf[4096];
  buf[0] = '\0';
  int fd = open(path, O_RDONLY);
  if (fd >= 0) {
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0)
      buf[n] = '\0';
    close(fd);
  }
  unlink(path);

  if (!trapped) {
    snprintf(g_detail, sizeof(g_detail), "fatal hook was never called");
    return 0;
  }
  if (g_hook_code != expect_code) {
    snprintf(g_detail, sizeof(g_detail), "fatal code: got %d, expected %d",
             g_hook_code, expect_code);
    return 0;
  }
  if (expect_kind && (!g_hook_kind || strcmp(g_hook_kind, expect_kind) != 0)) {
    snprintf(g_detail, sizeof(g_detail), "fatal kind: got \"%s\", expected \"%s\"",
             g_hook_kind ? g_hook_kind : "(null)", expect_kind);
    return 0;
  }
  if (expect_substr && !strstr(buf, expect_substr)) {
    snprintf(g_detail, sizeof(g_detail),
             "diagnostic missing \"%s\": got \"%s\"", expect_substr, buf);
    return 0;
  }
  return 1;
}

/* --------------------------- exception support -------------------------- */

static CPlusExceptionFrame g_exc_frame;
static int g_exc_seen;

int h_exception_roundtrip(int code, const char *message, const char *file,
                          int line, int *out_code, const char **out_message,
                          const char **out_file, int *out_line) {
  /* The frame is static, so its jmp_buf survives the longjmp performed by
   * cplus_exception_raise and its fields remain well-defined afterwards. */
  if (setjmp(g_exc_frame.env) == 0) {
    g_exc_seen = 0;
    cplus_exception_enter(&g_exc_frame);
    cplus_exception_raise(code, message, file, line);
    g_exc_seen = -1; /* unreachable unless raise returned */
    return g_exc_seen;
  }
  g_exc_seen = 1;
  if (out_code)
    *out_code = g_exc_frame.exc.code;
  if (out_message)
    *out_message = g_exc_frame.exc.message;
  if (out_file)
    *out_file = g_exc_frame.exc.file;
  if (out_line)
    *out_line = g_exc_frame.exc.line;
  return g_exc_seen;
}

/* -------------------------------- main ---------------------------------- */

void harness_init(void) {
  /* Unbuffered streams make dup2-based capture/injection deterministic. */
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  setvbuf(stdin, NULL, _IONBF, 0);
  cplus_fatal_hook = NULL;
  cplus_runtime_log_level = 99;
  g_detail[0] = '\0';
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  size_t pass = 0, fail = 0;
  harness_init();

  for (size_t i = 0; i < g_scenario_count; ++i) {
    g_detail[0] = '\0';
    int ok = g_scenarios[i].fn();
    if (ok) {
      ++pass;
      printf("PASS %s\n", g_scenarios[i].name);
    } else {
      ++fail;
      /* Keep one FAIL per line so the Python runner can parse it. */
      printf("FAIL %s | ", g_scenarios[i].name);
      const char *d = g_detail[0] ? g_detail : "no detail";
      for (const char *p = d; *p; ++p) {
        if (*p == '\n')
          fputs("\\n", stdout);
        else if (*p == '\r')
          fputs("\\r", stdout);
        else
          putchar(*p);
      }
      putchar('\n');
    }
    /* Scenarios must leave I/O and global hooks in a neutral state. */
    harness_init();
  }
  printf("SUMMARY scenarios=%zu passed=%zu failed=%zu\n", g_scenario_count,
         pass, fail);
  return fail == 0 ? 0 : 1;
}
