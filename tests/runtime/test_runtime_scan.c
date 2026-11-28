/*===-- test_runtime_scan.c - scan() destination-safety regressions --------===
 *
 * Deterministic regression tests for Agent 1 issue #111.  Every fatal path
 * is exercised in a forked child (exit 200 = the expected trap, 201 = wrong
 * exit code, 202 = stdin was consumed when it must still be pending), and
 * stream continuity is checked by recovering from the fatal hook with
 * longjmp so a second scan can prove the overlong token was fully consumed.
 *
 *===----------------------------------------------------------------------===*/

#include "cplus/cplus.h"

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "  FAIL %s (line %d)\n", (msg), __LINE__);               \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

static const char *const SCAN_INPUT_PATH = "build/runtime/scan_input.txt";

static void set_stdin_bytes(const char *text, size_t len) {
  FILE *f = fopen(SCAN_INPUT_PATH, "wb");
  if (!f) {
    fprintf(stderr, "  FAIL cannot create %s\n", SCAN_INPUT_PATH);
    ++failures;
    return;
  }
  if (len && fwrite(text, 1, len, f) != len) {
    fprintf(stderr, "  FAIL cannot write %s\n", SCAN_INPUT_PATH);
    ++failures;
  }
  fclose(f);
  if (!freopen(SCAN_INPUT_PATH, "r", stdin)) {
    fprintf(stderr, "  FAIL cannot reopen %s as stdin\n", SCAN_INPUT_PATH);
    ++failures;
  }
}

/* --- forked fatal cases --------------------------------------------------- */

typedef enum {
  CASE_FLOAT_UNDERSIZED,
  CASE_DOUBLE_UNDERSIZED,
  CASE_CHAR_ZERO,
  CASE_STR_ZERO,
  CASE_STR_OVERFLOW,
  CASE_STR_4096,
  CASE_STR_EOF
} scan_case;

static cplus_exit_code expected_code;
static int check_pending_char;
static int expected_pending_char;

static void hook_fatal(cplus_exit_code code, const char *kind) {
  (void)kind;
  if (code != expected_code)
    _Exit(201);
  if (check_pending_char) {
    /* Proves the rejected conversion did not read a single byte of input,
     * or (for overlong strings) that the delimiter was put back. */
    int c = fgetc(stdin);
    if (c != expected_pending_char)
      _Exit(202);
  }
  _Exit(200);
}

static void invoke_case(scan_case which) {
  static unsigned char buf[8192];
  memset(buf, 0x5a, sizeof(buf));
  switch (which) {
  case CASE_FLOAT_UNDERSIZED:
    cplus_scan_value_at(buf, 2u, CPLUS_SCAN_FLOAT, "f", NULL, 0);
    break;
  case CASE_DOUBLE_UNDERSIZED:
    cplus_scan_value_at(buf, sizeof(double) - 1u, CPLUS_SCAN_DOUBLE, "d", NULL,
                        0);
    break;
  case CASE_CHAR_ZERO:
    cplus_scan_value_at(buf, 0u, CPLUS_SCAN_CHAR, "c", NULL, 0);
    break;
  case CASE_STR_ZERO:
    cplus_scan_value_at(buf, 0u, CPLUS_SCAN_STR, "s", NULL, 0);
    break;
  case CASE_STR_OVERFLOW:
    cplus_scan_value_at(buf, 4u, CPLUS_SCAN_STR, "s", NULL, 0);
    break;
  case CASE_STR_4096: {
    static char big[4096];
    cplus_scan_value_at(big, sizeof(big), CPLUS_SCAN_STR, "big", NULL, 0);
    break;
  }
  case CASE_STR_EOF:
    cplus_scan_value_at(buf, 8u, CPLUS_SCAN_STR, "s", NULL, 0);
    break;
  }
}

static void expect_fatal(const char *name, scan_case which, const char *input,
                         size_t len, cplus_exit_code code, int pending_char) {
  fflush(stdout);
  pid_t pid = fork();
  if (pid < 0) {
    CHECK(0, "fork failed");
    return;
  }
  if (pid == 0) {
    set_stdin_bytes(input, len);
    expected_code = code;
    check_pending_char = pending_char >= 0;
    expected_pending_char = pending_char;
    cplus_fatal_hook = hook_fatal;
    invoke_case(which);
    _Exit(0); /* must be unreachable */
  }
  int status = 0;
  if (waitpid(pid, &status, 0) != pid) {
    CHECK(0, "waitpid failed");
    return;
  }
  if (!(WIFEXITED(status) && WEXITSTATUS(status) == 200)) {
    int rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    fprintf(stderr, "  FAIL %s: child rc=%d (200=ok, 201=wrong code, "
                    "202=stdin state) signal=%d\n",
            name, rc, WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    ++failures;
  }
}

static void test_undersized_destinations(void) {
  /* Size mismatch must trap before scanf() can write through dest, and must
   * leave the input untouched for a caller that recovers. */
  expect_fatal("float size=2", CASE_FLOAT_UNDERSIZED, "1.5\n", 4,
               CPLUS_EXIT_UNSUPPORTED, '1');
  expect_fatal("double size=7", CASE_DOUBLE_UNDERSIZED, "2.5\n", 4,
               CPLUS_EXIT_UNSUPPORTED, '2');
  expect_fatal("char size=0", CASE_CHAR_ZERO, "Z\n", 2,
               CPLUS_EXIT_UNSUPPORTED, 'Z');
  expect_fatal("str size=0", CASE_STR_ZERO, "hello\n", 6,
               CPLUS_EXIT_UNSUPPORTED, 'h');
}

static void test_overlong_strings(void) {
  /* "abcdef" into a 4-byte destination (3 characters + NUL) must fail
   * closed, consume the whole token, and leave only the delimiter pending. */
  expect_fatal("str overlong", CASE_STR_OVERFLOW, "abcdef NEXT\n", 12,
               CPLUS_EXIT_BOUNDS, ' ');
  /* 4096 bytes crosses the old 4095-byte scanf staging buffer: the entire
   * word must still be consumed, never split into two scan() calls. */
  char long_token[4096];
  memset(long_token, 'x', sizeof(long_token));
  char input[4098];
  memcpy(input, long_token, sizeof(long_token));
  input[4096] = ' ';
  input[4097] = '\0';
  expect_fatal("str 4096 bytes", CASE_STR_4096, input, 4097,
               CPLUS_EXIT_BOUNDS, ' ');
  expect_fatal("str eof", CASE_STR_EOF, "", 0, CPLUS_EXIT_IO, -1);
}

/* --- stream continuity after a recovered bounds failure ------------------ */

static jmp_buf recover_env;
static volatile int recovered_code;

static void hook_recover(cplus_exit_code code, const char *kind) {
  (void)kind;
  recovered_code = (int)code;
  longjmp(recover_env, 1);
}

static void test_no_stdin_desync(const char *input, size_t len,
                                 const char *name) {
  static char small[4]; /* capacity 3 */
  static char next[16];
  set_stdin_bytes(input, len);
  recovered_code = 0;
  cplus_fatal_hook = hook_recover;
  if (setjmp(recover_env) == 0) {
    cplus_scan_value_at(small, sizeof(small), CPLUS_SCAN_STR, "small", NULL, 0);
    CHECK(0, "no desync: expected a bounds failure");
  } else {
    CHECK(recovered_code == CPLUS_EXIT_BOUNDS, "no desync: wrong exit code");
  }
  cplus_fatal_hook = NULL;
  /* A rejected token must not leak its tail into the next scan. */
  cplus_scan_value_at(next, sizeof(next), CPLUS_SCAN_STR, "next", NULL, 0);
  CHECK(strcmp(next, "NEXT") == 0, "no desync: next scan saw the token tail");
  (void)name;
}

/* --- normal conversions still work --------------------------------------- */

static void test_valid_conversions(void) {
  set_stdin_bytes("3.5 2.75 Z hello\n", 17);
  float f = 0.0f;
  double d = 0.0;
  char c = '\0';
  char s[16];
  memset(s, 0, sizeof(s));
  cplus_scan_value_at(&f, sizeof(f), CPLUS_SCAN_FLOAT, "f", NULL, 0);
  cplus_scan_value_at(&d, sizeof(d), CPLUS_SCAN_DOUBLE, "d", NULL, 0);
  cplus_scan_value_at(&c, sizeof(c), CPLUS_SCAN_CHAR, "c", NULL, 0);
  cplus_scan_value_at(s, sizeof(s), CPLUS_SCAN_STR, "s", NULL, 0);
  CHECK(fabsf(f - 3.5f) < 0.0001f, "float conversion");
  CHECK(fabs(d - 2.75) < 0.0001, "double conversion");
  CHECK(c == 'Z', "char conversion");
  CHECK(strcmp(s, "hello") == 0, "string conversion");
}

static void test_exact_capacity_string(void) {
  set_stdin_bytes("abcd NEXT\n", 10);
  char s[5]; /* 4 characters + NUL: exactly fits */
  memset(s, 0, sizeof(s));
  char next[16];
  memset(next, 0, sizeof(next));
  cplus_scan_value_at(s, sizeof(s), CPLUS_SCAN_STR, "s", NULL, 0);
  CHECK(strcmp(s, "abcd") == 0, "string at exact capacity");
  cplus_scan_value_at(next, sizeof(next), CPLUS_SCAN_STR, "next", NULL, 0);
  CHECK(strcmp(next, "NEXT") == 0, "string at exact capacity: next scan");
}

int main(void) {
  test_undersized_destinations();
  test_overlong_strings();
  test_no_stdin_desync("abcdefghij NEXT\n", 16, "short overlong token");
  test_no_stdin_desync("abcdef NEXT\n", 12, "minimal overlong token");
  test_valid_conversions();
  test_exact_capacity_string();

  if (failures == 0) {
    printf("runtime_scan: ok\n");
    return 0;
  }
  printf("runtime_scan: %d check(s) failed\n", failures);
  return 1;
}
