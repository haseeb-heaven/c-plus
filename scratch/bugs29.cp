#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// ============================================================================
// 555. Unterminated source passed to an unbounded copy
// ============================================================================
void bug555_unterminated_strcpy(void) {
  char raw[4] = {'a', 'b', 'c', 'd'}; /* filled to capacity: no NUL */
  char dst[16];
  // BUG 555: strcpy reads past raw[] looking for a NUL that is not there.
  // Adjacent stack bytes are appended to dst, possibly overrunning it too.
  strcpy(dst, raw);
  // SAFE FIX: keep one byte for a terminator, set raw[3] = '\0' (or use
  // memcpy with the exact length) before copying into a sized destination.
  (void)dst;
}

// ============================================================================
// 556. strncpy fills the whole destination and omits the NUL
// ============================================================================
void bug556_strncpy_no_nul(void) {
  const char *src = "abcdefgh";
  char dst[8];
  // BUG 556: n equals the full capacity, so strncpy writes all eight bytes
  // without a terminator; the following puts reads beyond dst.
  strncpy(dst, src, sizeof dst);
  puts(dst);
  // SAFE FIX: pass sizeof dst - 1 and set dst[sizeof dst - 1] = '\0'.
}

// ============================================================================
// 557. stpncpy return value treated as a NUL-terminated end pointer
// ============================================================================
void bug557_stpncpy_return(const char *src) {
  char dst[12];
  // BUG 557: when src is at least sizeof dst bytes long, stpncpy returns
  // dst + sizeof dst (one past the end). Appending ':' and a NUL there
  // writes out of bounds; end never points at a writable terminator.
  char *end = stpncpy(dst, src, sizeof dst);
  *end++ = ':';
  *end = '\0';
  // SAFE FIX: check `end >= dst + sizeof dst` and use the last byte for ':'.
  (void)dst;
}

// ============================================================================
// 558. strxfrm truncation silently accepted as a complete sort key
// ============================================================================
void bug558_strxfrm_truncation(const char *text, size_t cap) {
  char key[16];
  size_t need;
  // BUG 558: the required length is larger than the buffer, but the code
  // ignores it and sorts on a truncated key; equal prefixes compare wrong.
  need = strxfrm(key, text, cap < sizeof key ? cap : sizeof key);
  if (need > 0)
    (void)strcmp(key, text);
  // SAFE FIX: allocate need + 1 bytes and call strxfrm again when the
  // returned length is greater than or equal to the supplied bound.
}

// ============================================================================
// 559. memccpy result ignored when the delimiter is not found
// ============================================================================
void bug559_memccpy_delimiter(const char *src) {
  char line[24];
  // BUG 559: memccpy returns NULL when no '\0' appears in the first
  // sizeof line bytes. The caller does not notice, so line is unterminated
  // and strlen/puts walk off the end.
  if (memccpy(line, src, '\0', sizeof line) == NULL) {
    /* delimiter absent: line is a fixed-size, unterminated fragment */
  }
  (void)strlen(line);
  // SAFE FIX: on NULL, terminate explicitly with line[sizeof line - 1] = 0.
}

// ============================================================================
// 560. Overlapping memcpy hidden behind a copy helper
// ============================================================================
static void bug560_copy_bytes(void *dst, const void *src, size_t n) {
  memcpy(dst, src, n);
}
void bug560_memcpy_overlap_helper(void) {
  char buf[32] = "abcdefghijklmnopqrstuvwxyz";
  // BUG 560: source and destination ranges overlap. memcpy has undefined
  // behaviour for overlap; the helper hides the relationship from a direct
  // call-site inspection.
  bug560_copy_bytes(buf + 2, buf, 12);
  puts(buf);
  // SAFE FIX: use memmove whenever the ranges can overlap.
}

// ============================================================================
// 561. strncat remaining-space underflow (no byte reserved for NUL)
// ============================================================================
void bug561_strncat_underflow(const char *suffix) {
  char dst[16] = "prefix:";
  size_t remaining = sizeof dst - strlen(dst);
  // BUG 561: remaining counts every free byte, but strncat also writes a
  // terminator, so a suffix of `remaining` bytes overflows dst by one.
  strncat(dst, suffix, remaining);
  // SAFE FIX: pass sizeof dst - strlen(dst) - 1 and clamp the length.
  (void)dst;
}

// ============================================================================
// 562. strlen on a buffer that has no terminator
// ============================================================================
void bug562_strlen_nonterminated(void) {
  char data[6] = {'h', 'e', 'l', 'l', 'o', '!'};
  // BUG 562: strlen walks past data[5] until it happens to find a zero byte,
  // producing a length unrelated to the six bytes actually stored.
  size_t len = strlen(data);
  printf("%zu\n", len);
  // SAFE FIX: store a NUL (data[5] = 0) or use strnlen with an explicit bound.
}

// ============================================================================
// 563. wcsncpy fills a wide buffer without a terminating L'\0'
// ============================================================================
void bug563_wcsncpy_no_nul(void) {
  const wchar_t *src = L"abcdefgh";
  wchar_t dst[8];
  // BUG 563: src is exactly as long as dst, so wcsncpy copies eight wide
  // characters and writes no L'\0'; the subsequent wprintf over-reads.
  wcsncpy(dst, src, 8);
  wprintf(L"%ls\n", dst);
  // SAFE FIX: copy at most 7 wide chars and set dst[7] = L'\0'.
}

// ============================================================================
// 564. mbsrtowcs source cursor reset after a truncated conversion
// ============================================================================
void bug564_mbsrtowcs_wrong_cursor(const char *input, size_t wlen) {
  wchar_t dst[32];
  mbstate_t state = {0};
  const char *cursor = input;
  const char *saved = cursor;
  // BUG 564: mbsrtowcs advances cursor to the first unconverted byte. The
  // code resumes from `saved`, converting the same prefix twice and dropping
  // the tail, so the assembled wide string is corrupt.
  (void)mbsrtowcs(dst, &cursor, wlen < 32 ? wlen : 32, &state);
  (void)mbsrtowcs(dst + 4, &saved, 8, &state);
  // SAFE FIX: keep using the advanced `cursor` and check for (size_t)-1.
  (void)dst;
}

int main(void) { return 0; }
