/* Stream positioning, transfer-count, close and buffering mistakes
   (bug IDs 475-484). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static char g_stream_buf[512];
static char g_small_buf[8];

/* ========================================================================== */
void bug475_read_then_write_no_seek(FILE *stream) {
  // BUG 475: An output operation directly after an input operation on an
  // update stream is undefined unless a positioning call intervenes.
  int c = fgetc(stream);
  (void)c;
  fputc('X', stream);
  // SAFE FIX: call fseek(stream, 0, SEEK_CUR) (or fflush) before writing.
}

/* ========================================================================== */
void bug476_write_after_eof_indicator(FILE *stream) {
  // BUG 476: After input hits EOF the indicator stays set; a subsequent write
  // on the same update stream has unspecified result until it is cleared.
  while (fgetc(stream) != EOF) {
  }
  fputc('Y', stream);
  // SAFE FIX: clearerr(stream) and reposition with fseek before writing.
}

/* ========================================================================== */
void bug477_text_stream_arbitrary_fseek(FILE *stream) {
  // BUG 477: Seeking to a hand-computed byte offset in a text stream is not
  // portable; only ftell-derived cookies or SEEK_SET 0 are meaningful.
  fseek(stream, 3, SEEK_SET);
  int c = fgetc(stream);
  (void)c;
  // SAFE FIX: open in binary mode, or seek only to offsets returned by ftell.
}

/* ========================================================================== */
void bug478_fread_count_product_overflow(FILE *stream, size_t size,
                                         size_t count) {
  // BUG 478: size * count can wrap in the library's internal calculation, so
  // the call reads far fewer bytes than the caller expects while still
  // appearing successful.
  char buf[64];
  size_t got = fread(buf, size, count, stream);
  (void)got;
  // SAFE FIX: reject size/count pairs whose product exceeds sizeof(buf).
}

/* ========================================================================== */
void bug479_fwrite_count_product_overflow(FILE *stream, size_t size,
                                          size_t count) {
  // BUG 479: The same product overflow on output silently truncates the
  // written data; a partial return value is mistaken for a full write.
  char buf[64] = {0};
  size_t put = fwrite(buf, size, count, stream);
  (void)put;
  // SAFE FIX: validate size * count against the buffer before calling.
}

/* ========================================================================== */
static void close_stream(FILE *stream) { fclose(stream); }

void bug480_cross_function_double_close(void) {
  // BUG 480: One function closes the stream and a later caller closes the
  // same FILE* again; the double close is not visible at the call site.
  FILE *stream = fopen("/tmp/bugcat-stream", "w");
  if (!stream)
    return;
  close_stream(stream);
  fclose(stream);
  // SAFE FIX: transfer ownership explicitly and set the pointer to NULL
  // after the first close.
}

/* ========================================================================== */
void bug481_ungetc_discarded_by_seek(FILE *stream) {
  // BUG 481: A character pushed back with ungetc is discarded by a subsequent
  // file-positioning call, so the next read returns the wrong byte.
  int c = fgetc(stream);
  ungetc(c, stream);
  fseek(stream, 0, SEEK_CUR);
  int next = fgetc(stream);
  (void)next;
  // SAFE FIX: consume the pushed-back character before seeking, or seek to
  // the desired position first and then push back.
}

/* ========================================================================== */
void bug482_ungetc_eof_result_ignored(FILE *stream) {
  // BUG 482: ungetc(EOF, stream) fails and leaves the stream unchanged, but
  // the caller assumes a character was pushed back.
  ungetc(EOF, stream);
  int c = fgetc(stream);
  (void)c;
  // SAFE FIX: check the return value of ungetc and handle EOF failure.
}

/* ========================================================================== */
void bug483_setvbuf_after_first_io(FILE *stream) {
  // BUG 483: Changing the buffering mode after I/O has started is undefined;
  // the library may already own an internal buffer.
  fputc('a', stream);
  setvbuf(stream, g_stream_buf, _IOFBF, sizeof g_stream_buf);
  // SAFE FIX: call setvbuf immediately after opening, before any I/O.
}

/* ========================================================================== */
void bug484_setvbuf_size_larger_than_buffer(FILE *stream) {
  // BUG 484: Claiming a 4096-byte buffer while supplying only 8 bytes makes
  // the library write past the array on the first flush.
  setvbuf(stream, g_small_buf, _IOFBF, 4096);
  // SAFE FIX: pass the real size (sizeof g_small_buf) or a matching buffer.
}

int main(void) { return 0; }
