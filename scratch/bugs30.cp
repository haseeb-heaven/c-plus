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
// 565. mbstate_t reused across independent wcrtomb calls
// ============================================================================
void bug565_wcrtomb_state_reuse(void) {
  mbstate_t state = {0};
  char out[MB_LEN_MAX];
  // BUG 565: the same conversion state is reused for two unrelated
  // characters. For stateful encodings the second call starts mid-shift,
  // producing a wrong byte sequence (or an error that is ignored).
  (void)wcrtomb(out, L'a', &state);
  (void)wcrtomb(out, L'b', &state);
  // SAFE FIX: zero a fresh mbstate_t for each independent conversion.
  (void)out;
}

// ============================================================================
// 566. Multibyte truncation with the return value ignored
// ============================================================================
void bug566_multibyte_truncation(const wchar_t *wide) {
  char dst[4];
  mbstate_t state = {0};
  // BUG 566: the wide string does not fit in four bytes. wcsrtombs stores a
  // truncated (and possibly mid-character) sequence; the caller ignores the
  // returned length and prints a broken multibyte string.
  (void)wcsrtombs(dst, &wide, sizeof dst, &state);
  puts(dst);
  // SAFE FIX: call with dst == NULL to learn the required size, allocate it,
  // then convert again and verify the return value is not (size_t)-1.
}

// ============================================================================
// 567. strtok re-entered from a callback during an active scan
// ============================================================================
static int bug567_token_consumer(const char *tok) { return tok ? 1 : 0; }
static char *bug567_advance_token(void) { return strtok(NULL, ","); }
static void bug567_scan(char *line, int (*cb)(const char *)) {
  char *t = strtok(line, ",");
  while (t) {
    (void)cb(t);
    t = bug567_advance_token();
  }
}
void bug567_strtok_callback_reentrancy(char *outer, char *inner) {
  // BUG 567: the callback starts a second strtok scan while bug567_scan is
  // still iterating. strtok keeps one global cursor, so the outer loop is
  // truncated and tokens are silently skipped.
  (void)bug567_scan(outer, &bug567_token_consumer);
  (void)bug567_scan(inner, &bug567_token_consumer);
  // SAFE FIX: use strtok_r/strtok_s with an explicit per-scan save pointer.
}

// ============================================================================
// 568. strndup with an unchecked pointer and a huge length
// ============================================================================
void bug568_strndup_huge_len(const char *maybe_null) {
  // BUG 568: the input pointer is never checked, and SIZE_MAX asks strndup
  // to scan far past the object. A NULL source crashes; a short source
  // makes strndup read unrelated memory until it finds a NUL.
  char *copy = strndup(maybe_null, (size_t)-1);
  if (copy)
    free(copy);
  // SAFE FIX: validate the pointer and bound n by the real buffer length.
}

// ============================================================================
// 569. strcpy of an fread payload that was never terminated
// ============================================================================
struct bug569_packet {
  char payload[8];
};
void bug569_fread_payload_strcpy(FILE *fp) {
  struct bug569_packet p;
  char line[32];
  // BUG 569: exactly eight bytes are read into payload with no room left
  // for a NUL, so strcpy scans into the following stack object.
  if (fread(p.payload, 1, sizeof p.payload, fp) == sizeof p.payload)
    strcpy(line, p.payload);
  // SAFE FIX: allocate sizeof payload + 1 and write payload[8] = '\0'.
  (void)line;
}

// ============================================================================
// 570. strncpy with a caller-supplied length larger than the destination
// ============================================================================
void bug570_strncpy_untrusted_len(const char *src, size_t n) {
  char dst[8];
  // BUG 570: n comes from an untrusted caller. When n > 8, strncpy writes
  // past dst; when n == 8 it fills dst without a terminator.
  strncpy(dst, src, n);
  // SAFE FIX: clamp n to sizeof dst - 1 and terminate explicitly.
  (void)dst;
}

// ============================================================================
// 571. Repeated strcat without tracking remaining capacity
// ============================================================================
static const char *bug570_next_item(int i) { return i ? "second" : "first"; }
void bug571_repeated_strcat(void) {
  char buf[16] = "";
  int i;
  // BUG 571: each append grows buf, but no code checks the remaining space.
  // After a few iterations the terminating NUL is written past the array.
  for (i = 0; i < 8; ++i)
    strcat(buf, bug570_next_item(i));
  // SAFE FIX: track the used length and use snprintf with the remaining size.
  (void)buf;
}

// ============================================================================
// 572. snprintf with an untrusted size argument
// ============================================================================
void bug572_snprintf_untrusted_cap(char *out, size_t cap) {
  // BUG 572: cap is supplied by the caller and can exceed the real size of
  // out. snprintf trusts it and writes beyond the allocation.
  (void)snprintf(out, cap, "%s/%s", "alpha", "beta");
  // SAFE FIX: pass the true object size, e.g. sizeof out at the call site.
}

// ============================================================================
// 573. memcpy with a runtime length that exceeds the source extent
// ============================================================================
void bug573_memcpy_untrusted_len(size_t n) {
  char src[8] = {0};
  char dst[64];
  // BUG 573: src holds eight bytes but n is attacker-controlled, so memcpy
  // copies uninitialised stack bytes (and overflows dst for large n).
  memcpy(dst, src, n);
  // SAFE FIX: validate n against sizeof src and sizeof dst before copying.
  (void)dst;
}

// ============================================================================
// 574. Negative length from a runtime helper cast to size_t
// ============================================================================
static int bug574_compute_len(void) { return -8; }
void bug574_negative_len_runtime(void) {
  int n = bug574_compute_len();
  char dst[16], src[16] = {0};
  // BUG 574: a failed computation returns -8; the cast to size_t turns it
  // into a gigantic length and memcpy walks off both buffers.
  memcpy(dst, src, (size_t)n);
  // SAFE FIX: check n >= 0 before the cast and pass a bounded length.
  (void)dst;
}

int main(void) { return 0; }
