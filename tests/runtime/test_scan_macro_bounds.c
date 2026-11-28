/*===-- test_scan_macro_bounds.c - scan() macro destination bounds ----------===
 *
 * Deterministic regression for Agent 1 issue #233.  The old cplus_scan_one()
 * passed `&x` (a char**) with a hardcoded 256-byte capacity for `char *x`, so
 * up to 255 input bytes plus a NUL were written into the pointer object and
 * past it.  This harness parks the pointer lvalue in the last bytes of a
 * writable page followed by a PROT_NONE guard page: any write into the pointer
 * slot itself crosses the guard and raises a fatal signal instead of silently
 * corrupting neighbours.  The guard is verified in a forked child first.
 *
 * A plain guard page is used instead of ASan because the host Clang/ASan
 * runtime cannot initialise here (sanitizer_malloc_mac.inc CHECK failure);
 * the guard gives a deterministic, platform-independent out-of-bounds signal.
 *
 *===----------------------------------------------------------------------===*/

#include "cplus/cplus.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
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

static const char *const INPUT_PATH = "build/runtime/scan_macro_input.txt";

static void set_stdin_bytes(const char *text, size_t len) {
  FILE *f = fopen(INPUT_PATH, "wb");
  if (!f) {
    CHECK(0, "cannot create scan input file");
    return;
  }
  if (len && fwrite(text, 1, len, f) != len)
    CHECK(0, "cannot write scan input file");
  fclose(f);
  if (!freopen(INPUT_PATH, "r", stdin))
    CHECK(0, "cannot reopen scan input as stdin");
}

typedef struct guarded_page {
  char *base;
  size_t page;
} guarded_page;

static guarded_page guard_new(void) {
  guarded_page g;
  g.page = (size_t)sysconf(_SC_PAGESIZE);
  g.base = (char *)mmap(NULL, g.page * 2u, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
  if (g.base == MAP_FAILED) {
    CHECK(0, "mmap of guarded region failed");
    g.base = NULL;
    return g;
  }
  if (mprotect(g.base + g.page, g.page, PROT_NONE) != 0) {
    CHECK(0, "mprotect of guard page failed");
    munmap(g.base, g.page * 2u);
    g.base = NULL;
  }
  return g;
}

/* Prove the guard really faults before relying on it to detect a bad write. */
static void guard_verify(guarded_page *g) {
  pid_t pid = fork();
  if (pid < 0) {
    CHECK(0, "fork for guard verification failed");
    return;
  }
  if (pid == 0) {
    g->base[g->page] = 1; /* must fault */
    _Exit(0);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) != pid) {
    CHECK(0, "waitpid for guard verification failed");
    return;
  }
  /* macOS raises SIGBUS, Linux SIGSEGV, for a write into a PROT_NONE page. */
  CHECK(WIFSIGNALED(status) &&
            (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS),
        "guard page did not trap a write");
}

static void test_char_pointer_lvalue(guarded_page *g) {
  static char dest[CPLUS_SCAN_PTR_CAPACITY];
  char input[201];
  memset(input, 'A', sizeof(input) - 1u);
  input[sizeof(input) - 1u] = '\0';

  /* The pointer object itself sits flush against the guard page. */
  char **slot = (char **)(g->base + g->page - sizeof(char *));
  *slot = dest;
  set_stdin_bytes(input, sizeof(input) - 1u);

  cplus_scan_one(*slot);

  CHECK(*slot == dest, "char * lvalue was overwritten by scan()");
  CHECK(strlen(dest) == sizeof(input) - 1u,
        "token was not written into the pointed-to buffer");
  CHECK(dest[0] == 'A' && dest[sizeof(input) - 2u] == 'A' &&
            dest[sizeof(input) - 1u] == '\0',
        "pointed-to buffer contents are wrong");
}

static void test_const_char_pointer_lvalue(guarded_page *g) {
  static char dest[CPLUS_SCAN_PTR_CAPACITY];
  const char **slot = (const char **)(g->base + g->page - sizeof(const char *));
  *slot = dest;
  set_stdin_bytes("world", 5u);

  cplus_scan_one(*slot);

  CHECK(*slot == dest, "const char * lvalue was overwritten by scan()");
  CHECK(strcmp(dest, "world") == 0,
        "const char * string did not reach the pointed-to buffer");
}

static void test_unsigned_promotion(void) {
  unsigned char uc = 0;
  unsigned short us = 0;

  set_stdin_bytes("200", 3u);
  cplus_scan_one(uc);
  CHECK(uc == 200u, "unsigned char was scanned with signed bounds");

  set_stdin_bytes("60000", 5u);
  cplus_scan_one(us);
  CHECK(us == 60000u, "unsigned short was scanned with signed bounds");
}

static void test_existing_destinations(void) {
  char arr[8];
  int n = 0;
  float f = 0.f;

  set_stdin_bytes("abc", 3u);
  cplus_scan_one(arr);
  CHECK(strcmp(arr, "abc") == 0, "char array destination regressed");

  set_stdin_bytes("42", 2u);
  cplus_scan_one(n);
  CHECK(n == 42, "int destination regressed");

  set_stdin_bytes("2.5", 3u);
  cplus_scan_one(f);
  CHECK(f == 2.5f, "float destination regressed");
}

int main(void) {
  guarded_page g = guard_new();
  if (!g.base) {
    fprintf(stderr, "scan_macro_bounds: setup failed\n");
    return 1;
  }
  guard_verify(&g);
  test_char_pointer_lvalue(&g);
  test_const_char_pointer_lvalue(&g);
  test_unsigned_promotion();
  test_existing_destinations();
  munmap(g.base, g.page * 2u);

  if (failures != 0) {
    fprintf(stderr, "scan_macro_bounds: %d failure(s)\n", failures);
    return 1;
  }
  printf("scan_macro_bounds: ok\n");
  return 0;
}
