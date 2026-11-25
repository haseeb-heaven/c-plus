#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <glob.h>
#ifndef __APPLE__
#include <iconv.h>
#else
/* macOS keeps iconv in a separate library; these stand-ins keep the
 * catalogue linkable on the verification host. */
typedef void *iconv_t;
static iconv_t iconv_open(const char *to, const char *from) {
  (void)to; (void)from;
  return (iconv_t)-1;
}
static int iconv_close(iconv_t cd) { (void)cd; return 0; }
#endif
#include <regex.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// 715. glob() result structure leaked (no globfree)
// ============================================================================
void bug715_glob_no_globfree(void) {
  glob_t g;
  if (glob("*.c", 0, NULL, &g) == 0) {
    // BUG 715: g.gl_pathv and every matched string are never released.
    // SAFE FIX: call globfree(&g) after the results are consumed.
  }
}

// ============================================================================
// 716. GLOB_APPEND chain accumulated without freeing the previous result
// ============================================================================
void bug716_glob_append_no_globfree(const char *p1, const char *p2) {
  glob_t g;
  if (glob(p1, 0, NULL, &g) == 0) {
    // BUG 716: appending requires the buffer from the first glob() to stay
    // valid, but the combined result is never globfree()'d either.
    glob(p2, GLOB_APPEND, NULL, &g);
    // SAFE FIX: globfree(&g) once the appended result is finished with.
  }
}

// ============================================================================
// 717. Saved gl_pathv entry used after globfree()
// ============================================================================
void bug717_glob_pathv_after_globfree(void) {
  glob_t g;
  if (glob("*.txt", 0, NULL, &g) == 0 && g.gl_pathc > 0) {
    char *saved = g.gl_pathv[0];
    globfree(&g);
    // BUG 717: saved points into storage released by globfree(); printing it
    // is a use-after-free.
    printf("%s\n", saved);
    // SAFE FIX: copy the string (strdup) before globfree(), or keep g alive.
  }
}

// ============================================================================
// 718. globfree() called on a glob_t that was never initialized/filled
// ============================================================================
void bug718_globfree_uninitialized(void) {
  glob_t g;
  // BUG 718: on GLOB_NOMATCH/glob() failure gl_pathv is not a valid pointer;
  // calling globfree() on this uninitialized structure is undefined.
  globfree(&g);
  // SAFE FIX: zero-initialize g and only globfree() on a successful glob().
}

// ============================================================================
// 719. regcomp() compiled regex never released with regfree()
// ============================================================================
void bug719_regcomp_no_regfree(const char *pattern) {
  regex_t re;
  if (regcomp(&re, pattern, REG_EXTENDED) == 0) {
    // BUG 719: the compiled regex owns allocated tables that regfree() would
    // release; dropping re on the stack leaks them.
    // SAFE FIX: call regfree(&re) after the last regexec().
  }
}

// ============================================================================
// 720. regfree() called before the final regexec()
// ============================================================================
int bug720_regfree_before_use(const char *pattern, const char *text) {
  regex_t re;
  int rc = regcomp(&re, pattern, REG_EXTENDED);
  if (rc != 0)
    return rc;
  regfree(&re);
  // BUG 720: regexec() now reads a regex_t whose internals were released.
  rc = regexec(&re, text, 0, NULL, 0);
  // SAFE FIX: regfree(&re) only AFTER the last regexec().
  return rc;
}

// ============================================================================
// 721. regexec() success ignored (match state used without checking)
// ============================================================================
void bug721_regexec_match_count_ignored(const char *pattern, const char *text) {
  regex_t re;
  regmatch_t m[1];
  if (regcomp(&re, pattern, REG_EXTENDED) == 0) {
    // BUG 721: when there is no match, regexec() leaves m[] untouched, but
    // the code proceeds as if m[0] held a valid range.
    regexec(&re, text, 1, m, 0);
    (void)m;
    regfree(&re);
    // SAFE FIX: check the regexec() return value before reading regmatch_t.
  }
}

// ============================================================================
// 722. iconv_open() conversion descriptor leaked
// ============================================================================
void bug722_iconv_no_close(void) {
  iconv_t cd = iconv_open("UTF-8", "ISO-8859-1");
  if (cd != (iconv_t)-1) {
    // BUG 722: conversion descriptors hold allocator/converter state that is
    // released only by iconv_close().
    // SAFE FIX: call iconv_close(cd).
  }
}

// ============================================================================
// 723. Overwriting an iconv descriptor without closing the old one
// ============================================================================
void bug723_iconv_reassign_leak(void) {
  iconv_t cd = iconv_open("UTF-8", "ISO-8859-1");
  // BUG 723: the first converter is lost and can never be closed.
  cd = iconv_open("UTF-16LE", "UTF-8");
  if (cd != (iconv_t)-1)
    iconv_close(cd);
  // SAFE FIX: iconv_close(cd) before assigning a new descriptor.
}

// ============================================================================
// 724. iconv_close() called with the wrong descriptor
// ============================================================================
void bug724_iconv_close_wrong_cd(const char *in, const char *out) {
  iconv_t a = iconv_open(out, in);
  iconv_t b = iconv_open(out, in);
  if (a != (iconv_t)-1 && b != (iconv_t)-1) {
    // BUG 724: closing 'a' twice while 'b' is still live leaks b and may
    // double-release shared converter state.
    iconv_close(a);
    iconv_close(a);
    // SAFE FIX: iconv_close(a); iconv_close(b); each exactly once.
  }
}

int main(void) { return 0; }
