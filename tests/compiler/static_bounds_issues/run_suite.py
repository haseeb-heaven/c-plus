#!/usr/bin/env python3
"""Deterministic regressions for compiler issues #152-#157.

Every case is compiled with `c+ check --sanitize=none`.  `reject=True` means
the case must fail with the expected diagnostic; `reject=False` means the
case must compile cleanly.  No game assets, network or live injection needed.

Usage:
    CPLUS_BIN=/path/to/c+ python3 tests/compiler/static_bounds_issues/run_suite.py
    CPLUS_BIN=/path/to/c+ python3 tests/compiler/static_bounds_issues/run_suite.py 154 155
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
PASS = 0
FAIL = 0

THRD_DECLS = r'''
typedef unsigned long thrd_t;
extern int cplus_thrd_create(thrd_t *t, int (*fn)(void *), void *arg);
extern int thrd_join(thrd_t t, int *res);
extern int cplus_setenv(const char *name, const char *value, int overwrite);
'''

# (name, should_reject, source, required diagnostic fragment)
CASES = {
    # #152 -- restrict aliasing must stay local to the function signature.
    152: [
        ("unrelated_restrict_call_ok", False, r'''
static void f(int *a, int *b) { *a = 1; *b = 2; }
static void g(int *restrict p, int *restrict q) { *p = 1; *q = 2; }
int main(void) { int y = 0; int *x = &y; f(x, x); return 0; }
''', ""),
        ("restrict_alias_reject", True, r'''
static void f(int *a, int *b) { *a = 1; *b = 2; }
static void g(int *restrict p, int *restrict q) { *p = 1; *q = 2; }
int main(void) { int y = 0; int *x = &y; g(x, x); return 0; }
''', "overlapping pointers to restrict parameters"),
        ("restrict_other_api_present_ok", False, r'''
#include <string.h>
static void f(int *a, int *b) { *a = 1; *b = 2; }
int main(void) { char buf[8] = {0}; memcpy(buf, buf + 2, 2); int y = 0; int *x = &y; f(x, x); return 0; }
''', ""),
    ],
    # #153 -- data races: substring locks, one worker vs main, C+ spawn APIs.
    153: [
        ("clock_and_block_still_race", True, r'''
#include <time.h>
#include <pthread.h>
static int g_shared;
static void *worker(void *arg) {
  int block = 0;
  (void)block;
  (void)clock();
  g_shared++;
  return 0;
}
int main(void) {
  pthread_t t;
  pthread_create(&t, 0, worker, 0);
  g_shared++;
  pthread_join(t, 0);
  return 0;
}
''', "unsynchronized write to shared global 'g_shared'"),
        ("two_workers_still_race", True, r'''
#include <pthread.h>
static int g_shared;
static void *worker(void *arg) { (void)arg; g_shared++; return 0; }
int main(void) {
  pthread_t a, b;
  pthread_create(&a, 0, worker, 0);
  pthread_create(&b, 0, worker, 0);
  pthread_join(a, 0);
  pthread_join(b, 0);
  return 0;
}
''', "unsynchronized write to shared global 'g_shared'"),
        ("cplus_thrd_create_main_race", True, THRD_DECLS + r'''
#include <unistd.h>
static int g_shared;
static int worker(void *arg) { (void)arg; g_shared++; return 0; }
int main(void) {
  thrd_t t = 0;
  cplus_thrd_create(&t, worker, 0);
  g_shared++;
  thrd_join(t, 0);
  return 0;
}
''', "unsynchronized write to shared global 'g_shared'"),
        ("balanced_mutex_ok", False, r'''
#include <pthread.h>
static pthread_mutex_t m;
static int g_shared;
static void *worker(void *arg) {
  (void)arg;
  pthread_mutex_lock(&m);
  g_shared++;
  pthread_mutex_unlock(&m);
  return 0;
}
int main(void) {
  pthread_t a, b;
  pthread_mutex_lock(&m);
  g_shared = 0;
  pthread_mutex_unlock(&m);
  pthread_create(&a, 0, worker, 0);
  pthread_create(&b, 0, worker, 0);
  pthread_join(a, 0);
  pthread_join(b, 0);
  return 0;
}
''', ""),
    ],
    # #154 -- fork/spawn ordering and C+ thread wrappers.
    154: [
        ("fork_before_spawn_ok", False, r'''
#include <unistd.h>
#include <pthread.h>
static void *worker(void *arg) { (void)arg; return 0; }
int main(void) {
  pid_t p = fork();
  if (p == 0) return 0;
  pthread_t t;
  pthread_create(&t, 0, worker, 0);
  pthread_join(t, 0);
  return 0;
}
''', ""),
        ("spawn_then_fork_reject", True, r'''
#include <unistd.h>
#include <pthread.h>
static void *worker(void *arg) { (void)arg; return 0; }
int main(void) {
  pthread_t t;
  pthread_create(&t, 0, worker, 0);
  pid_t p = fork();
  (void)p;
  pthread_join(t, 0);
  return 0;
}
''', "fork() in a multi-threaded process can deadlock"),
        ("fork_in_worker_reject", True, r'''
#include <unistd.h>
#include <pthread.h>
static void *worker(void *arg) {
  (void)arg;
  pid_t p = fork();
  (void)p;
  return 0;
}
int main(void) {
  pthread_t t;
  pthread_create(&t, 0, worker, 0);
  pthread_join(t, 0);
  return 0;
}
''', "fork() in a multi-threaded process can deadlock"),
        ("cplus_spawn_then_fork_reject", True, THRD_DECLS + r'''
#include <unistd.h>
static int worker(void *arg) { (void)arg; return 0; }
int main(void) {
  thrd_t t = 0;
  cplus_thrd_create(&t, worker, 0);
  pid_t p = fork();
  (void)p;
  thrd_join(t, 0);
  return 0;
}
''', "fork() in a multi-threaded process can deadlock"),
    ],
    # #155 -- every environment mutator invalidates earlier getenv results.
    155: [
        ("putenv_reject", True, r'''
#include <stdlib.h>
#include <stdio.h>
int f(void) {
  const char *v = getenv("X");
  putenv((char *)"X=Y");
  if (v) puts(v);
  return 0;
}
''', "getenv result 'v' may dangle after putenv"),
        ("unsetenv_reject", True, r'''
#include <stdlib.h>
#include <stdio.h>
int f(void) {
  const char *v = getenv("X");
  unsetenv("X");
  if (v) puts(v);
  return 0;
}
''', "getenv result 'v' may dangle after unsetenv"),
        ("cplus_setenv_reject", True, THRD_DECLS + r'''
#include <stdlib.h>
#include <stdio.h>
int f(void) {
  const char *v = getenv("X");
  cplus_setenv("X", "Y", 1);
  if (v) puts(v);
  return 0;
}
''', "getenv result 'v' may dangle after cplus_setenv"),
        ("repeated_getenv_reject", True, r'''
#include <stdlib.h>
#include <stdio.h>
int f(void) {
  const char *v = getenv("X");
  const char *w = getenv("Y");
  (void)w;
  if (v) puts(v);
  return 0;
}
''', "getenv result 'v' may dangle after getenv"),
        ("setenv_reject", True, r'''
#include <stdlib.h>
#include <stdio.h>
int f(void) {
  const char *v = getenv("X");
  setenv("X", "Y", 1);
  if (v) puts(v);
  return 0;
}
''', "getenv result 'v' may dangle after setenv"),
        ("no_mutation_ok", False, r'''
#include <stdlib.h>
#include <stdio.h>
int f(void) {
  const char *v = getenv("X");
  if (v) puts(v);
  return 0;
}
''', ""),
        ("mutation_before_getenv_ok", False, r'''
#include <stdlib.h>
#include <stdio.h>
int f(void) {
  setenv("X", "Y", 1);
  const char *v = getenv("X");
  if (v) puts(v);
  return 0;
}
''', ""),
    ],
    # #156 -- plain/signed char through <ctype.h> entrypoints.
    156: [
        ("char_parameter_reject", True, r'''
#include <ctype.h>
static int f(char c) { return isalpha(c); }
int main(void) { return f('a'); }
''', "cast a plain char to unsigned char"),
        ("signed_char_reject", True, r'''
#include <ctype.h>
int main(void) { signed char c = -1; return isdigit(c); }
''', "cast a plain char to unsigned char"),
        ("iscntrl_parameter_reject", True, r'''
#include <ctype.h>
static int f(char c) { return iscntrl(c); }
int main(void) { return f('\n'); }
''', "cast a plain char to unsigned char"),
        ("isgraph_reject", True, r'''
#include <ctype.h>
int main(void) { char c = 'a'; return isgraph(c); }
''', "cast a plain char to unsigned char"),
        ("isprint_reject", True, r'''
#include <ctype.h>
int main(void) { char c = 'a'; return isprint(c); }
''', "cast a plain char to unsigned char"),
        ("isblank_reject", True, r'''
#include <ctype.h>
int main(void) { char c = ' '; return isblank(c); }
''', "cast a plain char to unsigned char"),
        ("array_element_reject", True, r'''
#include <ctype.h>
int main(void) { char str[8] = {0}; int i = 0; return isalpha(str[i]); }
''', "cast a plain char to unsigned char"),
        ("pointer_deref_reject", True, r'''
#include <ctype.h>
int main(void) { char buf[8] = {0}; char *p = buf; return isspace(*p); }
''', "cast a plain char to unsigned char"),
        ("unsigned_char_ok", False, r'''
#include <ctype.h>
int main(void) { unsigned char c = 'a'; return isalpha(c); }
''', ""),
        ("explicit_cast_ok", False, r'''
#include <ctype.h>
static int f(char c) { return isalpha((unsigned char)c); }
int main(void) { return f('a'); }
''', ""),
    ],
    # #157 -- realloc self-assignment through members and array elements.
    157: [
        ("plain_self_realloc_reject", True, r'''
#include <stdlib.h>
int main(void) { char *p = malloc(8); if (!p) return 1; p = realloc(p, 16); free(p); return 0; }
''', "raw realloc can lose the original block"),
        ("struct_member_realloc_reject", True, r'''
#include <stdlib.h>
struct C { char *buf; };
int main(void) {
  struct C s;
  s.buf = malloc(8);
  if (!s.buf) return 1;
  s.buf = realloc(s.buf, 16);
  free(s.buf);
  return 0;
}
''', "raw realloc can lose the original block"),
        ("array_element_realloc_reject", True, r'''
#include <stdlib.h>
int main(void) {
  char *arr[2] = {0};
  arr[0] = malloc(8);
  if (!arr[0]) return 1;
  int i = 0;
  arr[i] = realloc(arr[i], 16);
  free(arr[0]);
  return 0;
}
''', "raw realloc can lose the original block"),
        ("guarded_realloc_ok", False, r'''
#include <stdlib.h>
int main(void) {
  char *p = malloc(8);
  if (!p) return 1;
  void *q = realloc(p, 16);
  if (!q) { free(p); return 1; }
  p = q;
  free(p);
  return 0;
}
''', ""),
    ],
}


def run_case(binary, name, reject, text, diagnostic):
    with tempfile.TemporaryDirectory(prefix="cplus_static_bounds_") as td:
        path = os.path.join(td, name + ".c")
        with open(path, "w") as handle:
            handle.write(text.lstrip("\n"))
        result = subprocess.run(
            [binary, "check", "--sanitize=none", path],
            capture_output=True, check=False, timeout=60)
    output = (result.stdout + result.stderr).decode(errors="replace")
    ok = result.returncode != 0 if reject else result.returncode == 0
    if reject and diagnostic:
        ok = ok and diagnostic in output
    return ok, f"rc={result.returncode} output={output[:500]!r}"


def main():
    selected = {int(arg) for arg in sys.argv[1:]} if len(sys.argv) > 1 else set(CASES)
    if not os.path.exists(BIN):
        print(f"missing c+ binary at {BIN}", file=sys.stderr)
        return 2
    global PASS, FAIL
    for issue in sorted(selected):
        if issue not in CASES:
            print(f"unknown issue {issue}", file=sys.stderr)
            return 2
        print(f"issue #{issue}")
        for name, reject, text, diagnostic in CASES[issue]:
            ok, detail = run_case(BIN, f"{issue}_{name}", reject, text, diagnostic)
            if ok:
                PASS += 1
                print(f"  ok   {name}")
            else:
                FAIL += 1
                print(f"  FAIL {name}: {detail}")
    print(f"\nSummary: {PASS} passed, {FAIL} failed")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
