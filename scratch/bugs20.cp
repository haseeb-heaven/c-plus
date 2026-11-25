/* Field width, precision, scanset and self-overlap formatting mistakes
   (bug IDs 465-474). */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

/* ========================================================================== */
void bug465_star_width_attacker_controlled(int width) {
  // BUG 465: A '*' width taken from untrusted input can request an enormous
  // field, forcing the C library to allocate/block for a very long time.
  printf("%*d", width, 1);
  // SAFE FIX: clamp the width to a small upper bound before formatting.
}

/* ========================================================================== */
void bug466_negative_star_width(int magnitude) {
  // BUG 466: A negative width is interpreted as left-justification; the
  // absolute value may be INT_MIN, whose negation is undefined.
  printf("%*d", -magnitude, 42);
  // SAFE FIX: validate the sign and reject values at INT_MIN before using it.
}

/* ========================================================================== */
void bug467_huge_star_precision(int precision, const char *text) {
  // BUG 467: A huge '*' precision makes printf walk far past the end of a
  // short string looking for a terminator.
  printf("%.*s", precision, text);
  // SAFE FIX: cap precision at the measured length of text.
}

/* ========================================================================== */
void bug468_width_count_multiply_overflow(int width, int items) {
  // BUG 468: Computing a field width as width * items can overflow signed
  // int and wrap to a negative or tiny width.
  int total = width * items;
  printf("%*d", total, 0);
  // SAFE FIX: check for overflow before multiplying and saturate the result.
}

/* ========================================================================== */
void bug469_negated_scanset_no_width(const char *line) {
  // BUG 469: %[^,] consumes an unbounded token into a fixed 8-byte buffer;
  // a long CSV field overflows the stack.
  char field[8];
  sscanf(line, "%[^,]", field);
  // SAFE FIX: use an explicit width: "%7[^,]".
}

/* ========================================================================== */
void bug470_fscanf_scanset_no_width(FILE *in) {
  // BUG 470: The same unbounded scanset problem applies to fscanf; no width
  // means the stream controls how many bytes land in digits[].
  char digits[6];
  fscanf(in, "%[0-9]", digits);
  // SAFE FIX: fscanf(in, "%5[0-9]", digits) and check the result.
}

/* ========================================================================== */
void bug471_snprintf_dest_overlaps_source(char *buf, size_t cap) {
  // BUG 471: The destination of snprintf overlaps a source argument; the
  // call overwrites bytes that %s is still reading, giving UB/garbage output.
  snprintf(buf, cap, "%s", buf + 1);
  // SAFE FIX: copy the source to a separate buffer before formatting.
}

/* ========================================================================== */
void bug472_wide_swprintf_self_overlap(wchar_t *buf, size_t cap) {
  // BUG 472: Wide formatting has the same overlap hazard as snprintf when
  // the destination is also passed as a %ls source.
  swprintf(buf, cap, L"%ls", buf + 1);
  // SAFE FIX: use a distinct source buffer for the %ls argument.
}

/* ========================================================================== */
void bug473_sprintf_two_overlapping_sources(char *buf, const char *tail) {
  // BUG 473: buf is both the sprintf destination and the first %s source;
  // writing the prefix destroys the string being consumed.
  sprintf(buf, "%s-%s", buf, tail);
  // SAFE FIX: format into a fresh buffer, then copy to buf.
}

/* ========================================================================== */
void bug474_precision_past_buffer(const char *small, int precision) {
  // BUG 474: A precision larger than the actual string length makes %.*s
  // read beyond the buffer's terminator.
  printf("%.*s", precision, small);
  // SAFE FIX: bound the precision by strlen(small) or a known buffer size.
}

int main(void) { return 0; }
