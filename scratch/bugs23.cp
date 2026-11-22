/* ftell/text-mode/stream-state mistakes (bug IDs 495-504). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static char g_text_buf[64];

/* ========================================================================== */
void bug495_ftell_cookie_as_byte_offset(FILE *stream) {
  // BUG 495: On a text stream ftell returns an opaque cookie, not a byte
  // count; adding 1 and seeking can land at the wrong character.
  long pos = ftell(stream);
  fseek(stream, pos + 1, SEEK_SET);
  // SAFE FIX: only seek to cookies returned by ftell, or open in binary mode.
}

/* ========================================================================== */
void bug496_ftell_after_error(FILE *stream) {
  // BUG 496: After a read error ftell may return -1; treating that as a valid
  // position and seeking to it moves the stream to an unintended place.
  while (fgetc(stream) != EOF) {
  }
  long pos = ftell(stream);
  if (pos > 0)
    fseek(stream, pos - 1, SEEK_SET);
  // SAFE FIX: call clearerr and check ferror before trusting ftell.
}

/* ========================================================================== */
void bug497_fseek_past_end_gap(FILE *stream) {
  // BUG 497: Seeking past EOF and writing creates a gap whose bytes read back
  // as unspecified values, which callers may mistake for real data.
  fseek(stream, 1000, SEEK_SET);
  fputc('x', stream);
  fseek(stream, 0, SEEK_SET);
  size_t n = fread(g_text_buf, 1, sizeof g_text_buf, stream);
  (void)n;
  // SAFE FIX: explicitly fill the gap with known bytes before reading it.
}

/* ========================================================================== */
void bug498_fseek_return_ignored(FILE *stream) {
  // BUG 498: A failed seek leaves the file position unchanged; ignoring the
  // result means the following read consumes the wrong record.
  fseek(stream, -5, SEEK_END);
  int c = fgetc(stream);
  (void)c;
  // SAFE FIX: test the fseek return value and abort the read on failure.
}

/* ========================================================================== */
void bug499_rewind_does_not_clear_error(FILE *stream) {
  // BUG 499: rewind resets the position but not the error indicator, so a
  // following write still observes the stale error state.
  (void)fgetc(stream);
  rewind(stream);
  fputs("fresh", stream);
  // SAFE FIX: call clearerr(stream) after rewind before further I/O.
}

/* ========================================================================== */
void bug500_ferror_confused_with_feof(FILE *stream) {
  // BUG 500: A read error sets ferror rather than feof; breaking when fread
  // returns 0 silently discards the fact that the stream is broken.
  char chunk[8];
  while (!feof(stream)) {
    size_t n = fread(chunk, 1, sizeof chunk, stream);
    if (n == 0)
      break;
  }
  // SAFE FIX: check ferror(stream) and report the I/O failure separately.
}

/* ========================================================================== */
void bug501_binary_data_in_text_mode(const char *path) {
  // BUG 501: Opening binary data in text mode applies newline translation,
  // so the bytes read back do not match the bytes stored on disk.
  FILE *stream = fopen(path, "r");
  if (!stream)
    return;
  size_t n = fread(g_text_buf, 1, 4, stream);
  (void)n;
  fclose(stream);
  // SAFE FIX: use the "rb" mode for binary payloads.
}

/* ========================================================================== */
void bug502_while_feof_antipattern(FILE *stream) {
  // BUG 502: Testing feof before reading processes the previous character a
  // second time because EOF is only set after a failed read.
  int c;
  while (!feof(stream)) {
    c = fgetc(stream);
    if (c != EOF)
      fputc(c, stdout);
  }
  // SAFE FIX: loop on the read result itself: while ((c = fgetc(f)) != EOF).
}

/* ========================================================================== */
void bug503_setvbuf_zero_size(FILE *stream) {
  // BUG 503: Passing size 0 together with a non-NULL buffer is undefined;
  // implementations may assume the buffer has at least one byte.
  setvbuf(stream, g_text_buf, _IOFBF, 0);
  // SAFE FIX: pass a positive size or use setbuf semantics with a real buffer.
}

/* ========================================================================== */
void bug504_freopen_drops_unflushed_data(FILE *stream) {
  // BUG 504: freopen discards whatever is still buffered in the old stream;
  // the fprintf output never reaches the file.
  fprintf(stream, "buffered payload");
  FILE *reopened = freopen("/tmp/bugcat-out", "w", stream);
  if (reopened)
    fclose(reopened);
  // SAFE FIX: fflush(stream) before freopen, or close and reopen explicitly.
}

int main(void) { return 0; }
