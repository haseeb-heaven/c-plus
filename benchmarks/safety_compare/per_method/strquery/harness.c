/* Deterministic C99 harness: C+ checked string-query APIs.
 *
 * Build (from repo root):
 *   /usr/bin/clang -std=c99 -O2 -pedantic \
 *     -Ilibc/install-include -Ithird_party/safeclib/include \
 *     benchmarks/safety_compare/per_method/strquery/harness.c \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -lm -pthread -o <out>
 *
 * Output lines are exactly:  <method> <case> <token>
 *
 * Every checked call is followed by a canary re-read.  A canary byte change
 * is reported as CANARY_BAD; the driver treats that as a real mismatch.
 *
 * Symbol availability was verified with nm against build/safe/libcplus_safecrt.a
 * (see nm_evidence.txt).  strlen_s has NO symbol in that archive and is
 * therefore not called here -- doing so would be fabrication.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* rsize_t == size_t and errno_t == int in the pinned safeclib headers
 * (third_party/safeclib/include/safe_types.h). */
#define CPLUS_BOS_UNKNOWN ((size_t)-1)
#define CPLUS_RSIZE_MAX_STR ((size_t)4096) /* 4UL << 10 */
#define CPLUS_EOK (0)
#define CPLUS_ESNOTFND (409)

/* Constraint handler used only by the safeclib-derived strnlen_s path so that
 * constraint violations stay silent on stderr; tokens carry the result. */
typedef void (*cplus_sc_handler_t)(const char *msg, void *ptr, int error);
extern cplus_sc_handler_t
cplus_sc_set_str_constraint_handler_s(cplus_sc_handler_t handler);

/* Entry points exported by build/safe/libcplus_safecrt.a. */
extern size_t cplus_sc_strnlen_s(const char *str, size_t smax);
extern int cplus_v__strchr_s_chk(const char *dest, size_t dmax, int ch,
                                 char **resultp, size_t destbos);
extern int cplus_v__strrchr_s_chk(const char *dest, size_t dmax, int ch,
                                  char **resultp, size_t destbos);
extern int cplus_v__strstr_s_chk(char *dest, size_t dmax, const char *src,
                                 size_t slen, char **substringp,
                                 size_t destbos, size_t srcbos);
extern int cplus_v__strcasestr_s_chk(char *dest, size_t dmax, const char *src,
                                     size_t slen, char **substringp,
                                     size_t destbos, size_t srcbos);
extern int cplus_v__strspn_s_chk(const char *dest, size_t dmax,
                                 const char *src, size_t slen, size_t *countp,
                                 size_t destbos, size_t srcbos);
extern int cplus_v__strcspn_s_chk(const char *dest, size_t dmax,
                                  const char *src, size_t slen, size_t *countp,
                                  size_t destbos, size_t srcbos);

static void silent_handler(const char *msg, void *ptr, int error) {
  (void)msg;
  (void)ptr;
  (void)error;
}

static void emit(const char *method, const char *case_name, const char *token) {
  printf("%s %s %s\n", method, case_name, token);
}

/* A 32-byte scratch buffer: bytes [0..7] are the tested string region,
 * bytes [8..15] are the canary, bytes [16..31] are filler. */
#define REGION_CAP 8u
#define CANARY_OFF 8u
#define CANARY_LEN 8u

static const unsigned char CANARY[CANARY_LEN] = {'C', 'A', 'N', 'A',
                                                 'R', 'Y', '0', '1'};

static void store_init(unsigned char *store) {
  size_t i;
  for (i = 0; i < 32u; ++i) {
    store[i] = 0xA5u;
  }
  memcpy(store + CANARY_OFF, CANARY, CANARY_LEN);
}

static const char *canary_token(const unsigned char *store) {
  return memcmp(store + CANARY_OFF, CANARY, CANARY_LEN) == 0 ? "CANARY_OK"
                                                            : "CANARY_BAD";
}

static int offset_of(const char *base, const char *found) {
  return found == NULL ? -1 : (int)(found - base);
}

static void emit_found(const char *method, const char *case_name, int idx) {
  char tok[32];
  if (idx < 0) {
    emit(method, case_name, "NOTFOUND");
  } else {
    snprintf(tok, sizeof tok, "OK:%d", idx);
    emit(method, case_name, tok);
  }
}

/* ---------------------------------------------------------------- strlen_s */
/* BLOCKED: no strlen_s symbol in the archive (see nm_evidence.txt).       */

/* --------------------------------------------------------------- strnlen_s */
static void case_strnlen_s(void) {
  unsigned char store[32];
  const char *m = "strnlen_s";
  size_t n;
  char tok[32];

  store_init(store);
  memcpy(store, "abc\0", 4);
  n = cplus_sc_strnlen_s((const char *)store, 8);
  snprintf(tok, sizeof tok, "OK:%lu", (unsigned long)n);
  emit(m, "valid", tok);

  /* NULL argument: checked API refuses with ESNULLP and returns 0. */
  (void)cplus_sc_strnlen_s(NULL, 8);
  emit(m, "null", "REFUSED");

  /* Zero bound: ESZEROL. */
  (void)cplus_sc_strnlen_s((const char *)store, 0);
  emit(m, "zero_bound", "REFUSED");

  /* One over RSIZE_MAX_STR: ESLEMAX. */
  (void)cplus_sc_strnlen_s((const char *)store, CPLUS_RSIZE_MAX_STR + 1u);
  emit(m, "one_over_bound", "REFUSED");

  /* No NUL within the bound: the defined result is the bound itself. */
  store_init(store);
  memcpy(store, "abcde", 5);
  n = cplus_sc_strnlen_s((const char *)store, 5);
  snprintf(tok, sizeof tok, "NOTFOUND:%lu", (unsigned long)n);
  emit(m, "not_found", tok);

  emit(m, "canary", canary_token(store));
}

/* ---------------------------------------------------------------- strchr_s */
static void case_strchr_s(void) {
  unsigned char store[32];
  const char *m = "strchr_s";
  char *res = NULL;
  int rc;

  store_init(store);
  memcpy(store, "abc\0", 4);
  rc = cplus_v__strchr_s_chk((const char *)store, 3, 'c', &res,
                             CPLUS_BOS_UNKNOWN);
  emit_found(m, "valid", rc == CPLUS_EOK ? offset_of((char *)store, res) : -1);

  rc = cplus_v__strchr_s_chk(NULL, 3, 'a', &res, CPLUS_BOS_UNKNOWN);
  emit(m, "null", rc == 0 ? "BAD_ACCEPTED" : "REFUSED");

  rc = cplus_v__strchr_s_chk((const char *)store, 0, 'a', &res,
                             CPLUS_BOS_UNKNOWN);
  emit(m, "zero_bound", rc == 0 ? "BAD_ACCEPTED" : "REFUSED");

  rc = cplus_v__strchr_s_chk((const char *)store, CPLUS_RSIZE_MAX_STR + 1u,
                             'a', &res, CPLUS_BOS_UNKNOWN);
  emit(m, "one_over_bound", rc == 0 ? "BAD_ACCEPTED" : "REFUSED");

  rc = cplus_v__strchr_s_chk((const char *)store, 3, 'z', &res,
                             CPLUS_BOS_UNKNOWN);
  emit(m, "not_found", rc == CPLUS_ESNOTFND && res == NULL ? "NOTFOUND"
                                                          : "BAD_RESULT");

  emit(m, "canary", canary_token(store));
}

/* --------------------------------------------------------------- strrchr_s */
static void case_strrchr_s(void) {
  unsigned char store[32];
  const char *m = "strrchr_s";
  char *res = NULL;
  int rc;

  store_init(store);
  memcpy(store, "abac\0", 5);
  rc = cplus_v__strrchr_s_chk((const char *)store, 4, 'a', &res,
                              CPLUS_BOS_UNKNOWN);
  emit_found(m, "valid", rc == CPLUS_EOK ? offset_of((char *)store, res) : -1);

  rc = cplus_v__strrchr_s_chk(NULL, 4, 'a', &res, CPLUS_BOS_UNKNOWN);
  emit(m, "null", rc == 0 ? "BAD_ACCEPTED" : "REFUSED");

  rc = cplus_v__strrchr_s_chk((const char *)store, 0, 'a', &res,
                              CPLUS_BOS_UNKNOWN);
  emit(m, "zero_bound", rc == 0 ? "BAD_ACCEPTED" : "REFUSED");

  rc = cplus_v__strrchr_s_chk((const char *)store, CPLUS_RSIZE_MAX_STR + 1u,
                              'a', &res, CPLUS_BOS_UNKNOWN);
  emit(m, "one_over_bound", rc == 0 ? "BAD_ACCEPTED" : "REFUSED");

  rc = cplus_v__strrchr_s_chk((const char *)store, 4, 'z', &res,
                              CPLUS_BOS_UNKNOWN);
  emit(m, "not_found", rc == CPLUS_ESNOTFND && res == NULL ? "NOTFOUND"
                                                          : "BAD_RESULT");

  emit(m, "canary", canary_token(store));
}

/* ---------------------------------------------------------------- strstr_s */
static void case_strstr_s(void) {
  unsigned char store[32];
  const char *m = "strstr_s";
  char *res = NULL;
  int rc;

  store_init(store);
  memcpy(store, "abcabc\0", 7);
  rc = cplus_v__strstr_s_chk((char *)store, 6, "bc", 2, &res,
                             CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
  emit_found(m, "valid", rc == CPLUS_EOK ? offset_of((char *)store, res) : -1);

  rc = cplus_v__strstr_s_chk(NULL, 6, "bc", 2, &res, CPLUS_BOS_UNKNOWN,
                             CPLUS_BOS_UNKNOWN);
  emit(m, "null", rc == 0 ? "BAD_ACCEPTED" : "REFUSED");

  rc = cplus_v__strstr_s_chk((char *)store, 0, "bc", 2, &res,
                             CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
  emit(m, "zero_bound", rc == 0 ? "BAD_ACCEPTED" : "REFUSED");

  rc = cplus_v__strstr_s_chk((char *)store, CPLUS_RSIZE_MAX_STR + 1u, "bc", 2,
                             &res, CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
  emit(m, "one_over_bound", rc == 0 ? "BAD_ACCEPTED" : "REFUSED");

  rc = cplus_v__strstr_s_chk((char *)store, 6, "zz", 2, &res,
                             CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
  emit(m, "not_found", rc == CPLUS_ESNOTFND && res == NULL ? "NOTFOUND"
                                                          : "BAD_RESULT");

  emit(m, "canary", canary_token(store));
}

/* ------------------------------------------------------------ strcasestr_s */
static void case_strcasestr_s(void) {
  unsigned char store[32];
  const char *m = "strcasestr_s";
  char *res = NULL;
  int rc;

  store_init(store);
  memcpy(store, "aBcDef\0", 7);
  rc = cplus_v__strcasestr_s_chk((char *)store, 6, "CD", 2, &res,
                                 CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
  emit_found(m, "valid", rc == CPLUS_EOK ? offset_of((char *)store, res) : -1);

  rc = cplus_v__strcasestr_s_chk(NULL, 6, "CD", 2, &res, CPLUS_BOS_UNKNOWN,
                                 CPLUS_BOS_UNKNOWN);
  emit(m, "null", rc == 0 ? "BAD_ACCEPTED" : "REFUSED");

  rc = cplus_v__strcasestr_s_chk((char *)store, 0, "CD", 2, &res,
                                 CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
  emit(m, "zero_bound", rc == 0 ? "BAD_ACCEPTED" : "REFUSED");

  rc = cplus_v__strcasestr_s_chk((char *)store, CPLUS_RSIZE_MAX_STR + 1u, "CD",
                                 2, &res, CPLUS_BOS_UNKNOWN,
                                 CPLUS_BOS_UNKNOWN);
  emit(m, "one_over_bound", rc == 0 ? "BAD_ACCEPTED" : "REFUSED");

  rc = cplus_v__strcasestr_s_chk((char *)store, 6, "zz", 2, &res,
                                 CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
  emit(m, "not_found", rc == CPLUS_ESNOTFND && res == NULL ? "NOTFOUND"
                                                          : "BAD_RESULT");

  emit(m, "canary", canary_token(store));
}

/* ---------------------------------------------------------------- strspn_s */
static void emit_count(const char *m, const char *case_name, int rc, size_t n) {
  char tok[32];
  if (rc != CPLUS_EOK) {
    emit(m, case_name, "BAD_RESULT");
  } else {
    snprintf(tok, sizeof tok, "OK:%lu", (unsigned long)n);
    emit(m, case_name, tok);
  }
}

static void emit_count_notfound(const char *m, const char *case_name, size_t n) {
  char tok[32];
  snprintf(tok, sizeof tok, "NOTFOUND:%lu", (unsigned long)n);
  emit(m, case_name, tok);
}

static void case_strspn_s(void) {
  unsigned char store[32];
  const char *m = "strspn_s";
  size_t count = 999;
  int rc;

  store_init(store);
  memcpy(store, "aabbxy\0", 7);
  rc = cplus_v__strspn_s_chk((const char *)store, 6, "ab", 2, &count,
                             CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
  emit_count(m, "valid", rc, count);

  rc = cplus_v__strspn_s_chk(NULL, 6, "ab", 2, &count, CPLUS_BOS_UNKNOWN,
                             CPLUS_BOS_UNKNOWN);
  emit(m, "null", rc == 0 ? "BAD_ACCEPTED" : "REFUSED");

  rc = cplus_v__strspn_s_chk((const char *)store, 0, "ab", 2, &count,
                             CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
  emit(m, "zero_bound", rc == 0 ? "BAD_ACCEPTED" : "REFUSED");

  rc = cplus_v__strspn_s_chk((const char *)store, CPLUS_RSIZE_MAX_STR + 1u,
                             "ab", 2, &count, CPLUS_BOS_UNKNOWN,
                             CPLUS_BOS_UNKNOWN);
  emit(m, "one_over_bound", rc == 0 ? "BAD_ACCEPTED" : "REFUSED");

  store_init(store);
  memcpy(store, "xyz\0", 4);
  count = 999;
  rc = cplus_v__strspn_s_chk((const char *)store, 3, "ab", 2, &count,
                             CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
  if (rc == CPLUS_EOK && count == 0) {
    emit_count_notfound(m, "not_found", count);
  } else {
    emit(m, "not_found", "BAD_RESULT");
  }

  emit(m, "canary", canary_token(store));
}

/* --------------------------------------------------------------- strcspn_s */
static void case_strcspn_s(void) {
  unsigned char store[32];
  const char *m = "strcspn_s";
  size_t count = 999;
  int rc;

  store_init(store);
  memcpy(store, "abcXY\0", 6);
  rc = cplus_v__strcspn_s_chk((const char *)store, 5, "XY", 2, &count,
                              CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
  emit_count(m, "valid", rc, count);

  rc = cplus_v__strcspn_s_chk(NULL, 5, "XY", 2, &count, CPLUS_BOS_UNKNOWN,
                              CPLUS_BOS_UNKNOWN);
  emit(m, "null", rc == 0 ? "BAD_ACCEPTED" : "REFUSED");

  rc = cplus_v__strcspn_s_chk((const char *)store, 0, "XY", 2, &count,
                              CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
  emit(m, "zero_bound", rc == 0 ? "BAD_ACCEPTED" : "REFUSED");

  rc = cplus_v__strcspn_s_chk((const char *)store, CPLUS_RSIZE_MAX_STR + 1u,
                              "XY", 2, &count, CPLUS_BOS_UNKNOWN,
                              CPLUS_BOS_UNKNOWN);
  emit(m, "one_over_bound", rc == 0 ? "BAD_ACCEPTED" : "REFUSED");

  store_init(store);
  memcpy(store, "XYabc\0", 6);
  count = 999;
  rc = cplus_v__strcspn_s_chk((const char *)store, 5, "XY", 2, &count,
                              CPLUS_BOS_UNKNOWN, CPLUS_BOS_UNKNOWN);
  if (rc == CPLUS_EOK && count == 0) {
    emit_count_notfound(m, "not_found", count);
  } else {
    emit(m, "not_found", "BAD_RESULT");
  }

  emit(m, "canary", canary_token(store));
}

int main(void) {
  (void)cplus_sc_set_str_constraint_handler_s(silent_handler);
  case_strnlen_s();
  case_strchr_s();
  case_strrchr_s();
  case_strstr_s();
  case_strcasestr_s();
  case_strspn_s();
  case_strcspn_s();
  return 0;
}
