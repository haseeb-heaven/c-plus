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
// 595. memcpy with a runtime length larger than the source extent
// ============================================================================
void bug595_memcpy_overread_runtime(size_t n) {
  char src[8] = {0};
  char dst[64];
  // BUG 595: the caller controls n. With n > 8, memcpy exposes uninitialised
  // stack bytes beyond src; with n > 64 it also overflows dst. The length is
  // a variable here, so a constant-extent check cannot see the violation.
  memcpy(dst, src, n);
  // SAFE FIX: validate n <= sizeof src and n <= sizeof dst at the boundary.
  (void)dst;
}

// ============================================================================
// 596. Overlapping memcpy with computed offsets behind a helper
// ============================================================================
static void bug596_copy(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void bug596_memcpy_computed_overlap(size_t shift) {
  char buf[64] = "0123456789abcdef";
  // BUG 596: the ranges overlap whenever shift < n, but the relationship is
  // expressed through variables and a helper call, so no direct overlap
  // check fires and the copy result is undefined.
  bug596_copy(buf + shift, buf, 16);
  // SAFE FIX: use memmove and validate that shift and n stay in bounds.
  (void)buf;
}

// ============================================================================
// 597. memmove byte count computed with an overflowing multiplication
// ============================================================================
void bug597_memmove_count_overflow(size_t count, size_t elem) {
  char src[64] = {0}, dst[64];
  // BUG 597: count * elem can wrap to a small value, so the move silently
  // copies far less data than intended; alternatively it can exceed both
  // buffers when the caller passes large operands.
  size_t bytes = count * elem;
  memmove(dst, src, bytes);
  // SAFE FIX: check that count <= SIZE_MAX / elem and bytes <= capacities.
  (void)dst;
}

// ============================================================================
// 598. memcpy between overlapping members of the same struct
// ============================================================================
struct bug598_pair {
  char head[16];
  char tail[16];
};
void bug598_memcpy_struct_members(struct bug598_pair *p) {
  // BUG 598: &p->tail[0] overlaps &p->head[8] for the requested length;
  // the two fields are adjacent in one object, so memcpy's non-overlap
  // precondition is violated.
  memcpy(p->tail, p->head + 8, 16);
  // SAFE FIX: use memmove for any same-object copy with a shared range.
}

// ============================================================================
// 599. strcpy with a destination that overlaps the source
// ============================================================================
void bug599_strcpy_self_overlap(char *text) {
  // BUG 599: copying a string onto a later position of itself is undefined
  // behaviour under the C string contract, even though strcpy's direct
  // arguments are two distinct expressions.
  strcpy(text + 1, text);
  // SAFE FIX: use memmove with the exact length, or copy via a temporary.
}

// ============================================================================
// 600. size_t wrap in a length expression passed to memcpy
// ============================================================================
void bug600_memcpy_size_wrap(size_t have) {
  char src[8] = {0}, dst[64];
  // BUG 600: `have - 16` wraps to SIZE_MAX-15 when have < 16, turning a
  // small copy into an enormous one. The wrap happens before the call, so
  // the size argument itself looks like an ordinary size_t expression.
  memcpy(dst, src, have - 16);
  // SAFE FIX: reject have < 16 before subtracting and clamp to the buffers.
  (void)dst;
}

// ============================================================================
// 601. Allocation sized by a byte length that is used as an element count
// ============================================================================
void bug601_strlen_wrong_allocation(const char *text) {
  size_t bytes = strlen(text);
  // BUG 601: multiplying by two is meant to leave room for a separator and
  // terminator, but the later copy uses `bytes` as if it were a 2-byte
  // element count, so the true capacity is misreported.
  char *buf = (char *)malloc(bytes * 2);
  if (buf)
    (void)snprintf(buf, bytes, "%s%s", text, text);
  // SAFE FIX: compute the exact required size and pass it consistently.
  free(buf);
}

// ============================================================================
// 602. strncat with SIZE_MAX as the append count
// ============================================================================
void bug602_strncat_huge_count(const char *suffix, size_t untrusted) {
  char dst[32] = "";
  // BUG 602: an untrusted or mis-signed count (e.g. SIZE_MAX) makes strncat
  // behave like an unbounded copy; it stops only at a NUL in suffix.
  strncat(dst, suffix, untrusted);
  // SAFE FIX: pass sizeof dst - strlen(dst) - 1 as the count.
  (void)dst;
}

// ============================================================================
// 603. strxfrm with n == 0 misused as a sizing query result
// ============================================================================
void bug603_strxfrm_zero_query(const char *text) {
  // BUG 603: the sizing call returns the required length, but the buffer
  // allocated from it omits the terminating NUL that strxfrm always writes.
  size_t need = strxfrm(NULL, text, 0);
  char *key = (char *)malloc(need);
  if (key)
    (void)strxfrm(key, text, need);
  // SAFE FIX: allocate need + 1 bytes for the terminator.
  free(key);
}

// ============================================================================
// 604. wcsncpy source NUL leaves the rest of the buffer untouched
// ============================================================================
void bug604_wcsncpy_early_nul(void) {
  wchar_t dst[8];
  size_t i;
  // BUG 604: wcsncpy pads with L'\0' only after the source terminator, so
  // source bytes beyond the first NUL are not copied. The code assumes all
  // eight slots were filled by the copy and reads stale stack data.
  wcsncpy(dst, L"ab\0defgh", 8);
  for (i = 0; i < 8; ++i)
    (void)dst[i];
  // SAFE FIX: memset the destination to zero before the bounded copy, or
  // copy exactly the number of wide characters you intend to use.
}

int main(void) { return 0; }
