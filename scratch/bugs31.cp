#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <limits.h>
#include <locale.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// ============================================================================
// 575. strtok state shared across helper functions
// ============================================================================
static char *bug575_first(char *s) { return strtok(s, ","); }
static char *bug575_next(void) { return strtok(NULL, ","); }
void bug575_strtok_split_helpers(char *a, char *b) {
  char *first = bug575_first(a);
  // BUG 575: the second helper uses strtok(NULL, ...) and the third call
  // resets strtok's single static cursor to b. `first` is now dangling, but
  // it is printed afterwards. Each helper has only one call, so a per-call
  // check does not see the cross-function interaction.
  (void)bug575_next();
  (void)bug575_first(b);
  puts(first);
  // SAFE FIX: thread an explicit `char **saveptr` through every helper and
  // use strtok_r / strtok_s.
}

// ============================================================================
// 576. strtok delimiter string changed mid-scan
// ============================================================================
static char *bug576_split_colon(char *s) { return strtok(s, ":"); }
static char *bug576_continue_semicolon(void) { return strtok(NULL, ";"); }
void bug576_strtok_delim_change(char *line) {
  char *t1 = bug576_split_colon(line);
  // BUG 576: continuing a strtok scan with a different delimiter set does
  // not restart it; strtok keeps the previous cursor and the new separators
  // are interpreted from the middle of the string.
  char *t2 = bug576_continue_semicolon();
  printf("%s %s\n", t1 ? t1 : "", t2 ? t2 : "");
  // SAFE FIX: use one consistent delimiter set, or strtok_r with a fresh
  // save pointer when switching.
}

// ============================================================================
// 577. mbstate_t shared between two logical wide-to-byte messages
// ============================================================================
static void bug577_emit(const wchar_t *msg, mbstate_t *st, char *out) {
  // BUG 577: the caller passes the same conversion state for two unrelated
  // messages, so a shift sequence left over from the first message leaks
  // into the second and the bytes are decoded incorrectly.
  (void)wcsrtombs(out, &msg, 32, st);
}
void bug577_wcsrtombs_state_shared(void) {
  mbstate_t shared = {0};
  char a[32], b[32];
  bug577_emit(L"first", &shared, a);
  bug577_emit(L"second", &shared, b);
  puts(a);
  puts(b);
  // SAFE FIX: initialise a separate mbstate_t per message.
}

// ============================================================================
// 578. mbrtowc incomplete sequence reported as consumed
// ============================================================================
void bug578_mbrtowc_incomplete(const char *bytes, size_t n) {
  wchar_t wc = 0;
  mbstate_t st = {0};
  // BUG 578: a return value of (size_t)-2 means "incomplete, need more
  // input". Treating it as a successful conversion emits a bogus wide
  // character and leaves the byte cursor in the wrong place.
  size_t r = mbrtowc(&wc, bytes, n, &st);
  if (r == (size_t)-2)
    r = 0;
  (void)mbrtowc(&wc, bytes + r, n - r, &st);
  // SAFE FIX: accumulate more bytes before retrying; only advance by a
  // non-negative, successful return value.
}

// ============================================================================
// 579. strspn used on an unterminated buffer
// ============================================================================
void bug579_strspn_no_bound(void) {
  char bytes[5] = {'0', '1', '2', '3', '4'};
  // BUG 579: strspn scans until a byte outside the set or a NUL is found.
  // bytes[] has no NUL, so the scan continues into adjacent stack memory.
  size_t n = strspn(bytes, "0123456789");
  printf("%zu\n", n);
  // SAFE FIX: use strnspn-style bounded logic or terminate the buffer.
}

// ============================================================================
// 580. strcpy out of a fixed-size struct member with no terminator
// ============================================================================
struct bug580_record {
  char code[4];
  int id;
};
void bug580_strcpy_struct_padding(struct bug580_record *r) {
  char out[16];
  // BUG 580: code[] is exactly four bytes with no terminator; the following
  // int is part of the same object but not a valid string byte, so strcpy
  // copies it and keeps going.
  memcpy(r->code, "ABCD", 4);
  strcpy(out, r->code);
  // SAFE FIX: store a NUL in the field or copy with strncpy + explicit NUL.
  (void)out;
}

// ============================================================================
// 581. memmove length given in wide elements instead of bytes
// ============================================================================
void bug581_memmove_element_count(wchar_t *wide, size_t count) {
  // BUG 581: memmove's third argument is a byte count. Passing the number of
  // wchar_t elements copies too little on platforms where wchar_t is 2 or 4
  // bytes, leaving half of the wide string behind.
  memmove(wide + 1, wide, count);
  // SAFE FIX: pass count * sizeof(wchar_t).
}

// ============================================================================
// 582. strncmp with a fixed length larger than both buffers
// ============================================================================
void bug582_strncmp_fixed_length(const char *a, const char *b) {
  // BUG 582: comparing 256 bytes can read past the end of either short
  // string, because strncmp stops at a NUL only in both operands.
  if (strncmp(a, b, 256) == 0)
    puts("equal");
  // SAFE FIX: compare strlen(a) + 1 bytes at most, or use bounded lengths
  // obtained from the actual buffers.
}

// ============================================================================
// 583. strchr on a buffer truncated by strncpy without a terminator
// ============================================================================
void bug583_strchr_after_truncation(const char *src) {
  char dst[8];
  // BUG 583: when src is longer than 8, strncpy leaves dst unterminated.
  // strchr then scans into neighbouring stack memory looking for ':'.
  strncpy(dst, src, sizeof dst);
  char *p = strchr(dst, ':');
  if (p)
    *p = '\0';
  // SAFE FIX: terminate dst explicitly before calling any string function.
}

// ============================================================================
// 584. strxfrm/strcoll used with a changed locale
// ============================================================================
void bug584_strcoll_locale_mismatch(void) {
  const char *a = "a", *b = "b";
  char ka[16], kb[16];
  // BUG 584: sort keys are generated under "C" but compared under a
  // different current locale, so the transformed keys have no meaning.
  (void)setlocale(LC_COLLATE, "C");
  (void)strxfrm(ka, a, sizeof ka);
  (void)setlocale(LC_COLLATE, "");
  (void)strxfrm(kb, b, sizeof kb);
  (void)strcmp(ka, kb);
  // SAFE FIX: keep the locale fixed for key generation and comparison, or
  // use strcoll consistently on the original strings.
}

int main(void) { return 0; }
