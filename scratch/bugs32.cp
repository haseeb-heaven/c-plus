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
// 585. Buffer sized with strlen(A) but copied from a longer string B
// ============================================================================
void bug585_strlen_sized_wrong_source(const char *short_s,
                                      const char *long_s) {
  // BUG 585: the allocation is one byte too small for short_s and far too
  // small for long_s. strcpy from a *different* source than the one used by
  // strlen hides the classic missing-NUL bug from a source-matching check.
  char *dst = (char *)malloc(strlen(short_s));
  if (dst)
    strcpy(dst, long_s);
  // SAFE FIX: allocate strlen(actual_source) + 1 and copy from that source.
  if (dst)
    free(dst);
}

// ============================================================================
// 586. strdup result used directly as a strcat destination
// ============================================================================
void bug586_strdup_then_strcat(const char *prefix, const char *suffix) {
  // BUG 586: strdup allocates exactly strlen(prefix)+1 bytes. Appending a
  // suffix with strcat overwrites the terminator and the bytes after it.
  char *buf = strdup(prefix);
  if (buf)
    strcat(buf, suffix);
  // SAFE FIX: realloc to strlen(prefix)+strlen(suffix)+1 before appending.
  if (buf)
    free(buf);
}

// ============================================================================
// 587. strcpy into a realloc'd block at a nonzero offset
// ============================================================================
void bug587_realloc_offset_copy(char *base, size_t used, const char *text) {
  char *grown = (char *)realloc(base, used + 32);
  if (!grown)
    return;
  // BUG 587: after moving the cursor, only `used + 32 - used` bytes remain,
  // but text may be longer; the offset calculation forgets the terminator.
  strcpy(grown + used, text);
  // SAFE FIX: bound the copy by the remaining capacity minus one byte.
  free(grown);
}

// ============================================================================
// 588. strlen byte count used to size a wide-character buffer
// ============================================================================
void bug588_malloc_strlen_multibyte(const char *mb) {
  size_t bytes = strlen(mb);
  // BUG 588: a wchar_t array needs bytes/sizeof(wchar_t) elements, not
  // `bytes` elements. The allocation is 2-4x too large in the wrong unit,
  // and the mbstowcs bound is wrong too.
  wchar_t *wide = (wchar_t *)malloc(bytes * sizeof(wchar_t));
  if (wide)
    (void)mbstowcs(wide, mb, bytes);
  // SAFE FIX: allocate (bytes + 1) * sizeof(wchar_t) and bound by bytes + 1.
  free(wide);
}

// ============================================================================
// 589. strndup on a non-terminated source with an excessive length
// ============================================================================
void bug589_strndup_nonterminated(void) {
  char raw[6] = {'a', 'b', 'c', 'd', 'e', 'f'};
  // BUG 589: n is larger than the real object and raw has no terminator, so
  // strndup reads past raw until it encounters an unrelated zero byte.
  char *copy = strndup(raw, 64);
  if (copy)
    free(copy);
  // SAFE FIX: pass the true available length and terminate the source.
}

// ============================================================================
// 590. strcpy into a fixed-size shared-memory-style region
// ============================================================================
static char bug590_shared_region[32];
void bug590_strcpy_fixed_region(const char *message) {
  // BUG 590: the region is a fixed 32-byte shared buffer and message is
  // untrusted. strcpy has no bound and can corrupt adjacent region data.
  strcpy(bug590_shared_region, message);
  // SAFE FIX: use snprintf/shared_region with sizeof region as the bound.
}

// ============================================================================
// 591. sprintf into an exact-size allocation (no room for the NUL)
// ============================================================================
void bug591_sprintf_exact_alloc(const char *word) {
  size_t need = strlen("id=") + strlen(word);
  // BUG 591: the allocation omits the terminating byte that sprintf always
  // writes, so the NUL lands just past the end of the block.
  char *buf = (char *)malloc(need);
  if (buf)
    sprintf(buf, "id=%s", word);
  // SAFE FIX: allocate need + 1, or use snprintf with the true capacity.
  free(buf);
}

// ============================================================================
// 592. Truncated strncpy result compared as a complete string
// ============================================================================
void bug592_strncpy_then_compare(const char *src) {
  char dst[8];
  // BUG 592: a long src is truncated to 8 bytes with no terminator, then
  // strcmp walks off dst; even if a terminator existed, the code wrongly
  // assumes the full src was copied.
  strncpy(dst, src, sizeof dst);
  if (strcmp(dst, src) != 0)
    (void)strcat(dst, "...");
  // SAFE FIX: terminate explicitly and check the return length of strncpy.
}

// ============================================================================
// 593. wcscpy into a buffer allocated in bytes for char
// ============================================================================
void bug593_wcscpy_byte_sized_buffer(const wchar_t *src) {
  // BUG 593: 64 is a byte count, but wcscpy writes wide characters. On a
  // 4-byte-wchar_t target this permits only 16 characters plus terminator.
  wchar_t *dst = (wchar_t *)malloc(64);
  if (dst)
    wcscpy(dst, src);
  // SAFE FIX: allocate (wcslen(src) + 1) * sizeof(wchar_t).
  free(dst);
}

// ============================================================================
// 594. strncat with a current length computed from an unterminated buffer
// ============================================================================
void bug594_strncat_bad_current_len(void) {
  char buf[8] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
  // BUG 594: strlen(buf) first over-reads to find a NUL, producing a bogus
  // "current length" that is then used to decide how many bytes to append.
  size_t used = strlen(buf);
  size_t room = used < sizeof buf ? sizeof buf - used : 0;
  if (room)
    strncat(buf, "!", room);
  // SAFE FIX: keep an explicit length for the buffer and reserve a NUL.
}

int main(void) { return 0; }
