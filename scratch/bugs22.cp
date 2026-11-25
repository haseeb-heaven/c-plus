/* Variadic argument, format-attribute and temporary-file mistakes
   (bug IDs 485-494). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* ========================================================================== */
static void consume_first(va_list ap) { (void)va_arg(ap, int); }

void bug485_va_list_passed_by_value(const char *fmt, ...) {
  // BUG 485: On ABIs where va_list is an array type, passing it to a helper
  // advances the caller's list too; reading again yields garbage.
  va_list ap;
  va_start(ap, fmt);
  consume_first(ap);
  int second = va_arg(ap, int);
  (void)second;
  va_end(ap);
  // SAFE FIX: pass a va_copy(ap) to the helper, or use va_copy before each
  // independent traversal.
}

/* ========================================================================== */
static void read_one(va_list ap) { (void)va_arg(ap, int); }

void bug486_two_helpers_same_va_list(const char *fmt, ...) {
  // BUG 486: Two consumers share one consumed iterator; the second helper
  // starts at the wrong argument instead of at the beginning.
  va_list ap;
  va_start(ap, fmt);
  read_one(ap);
  read_one(ap);
  va_end(ap);
  // SAFE FIX: give each helper its own va_copy and end each copy.
}

/* ========================================================================== */
void bug487_missing_va_end_on_success(const char *fmt, ...) {
  // BUG 487: va_end is omitted on the ordinary return path, leaking the
  // iterator state and leaving the ABI's frame-registration bookkeeping open.
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  // SAFE FIX: always call va_end(ap) before returning.
}

/* ========================================================================== */
void bug488_va_arg_after_va_end(const char *fmt, ...) {
  // BUG 488: Reading a va_list after va_end is undefined; the iterator has
  // already been released to the ABI.
  va_list ap;
  va_start(ap, fmt);
  va_end(ap);
  int x = va_arg(ap, int);
  (void)x;
  // SAFE FIX: finish all argument reads before calling va_end.
}

/* ========================================================================== */
void bug489_va_arg_type_mismatch(const char *fmt, ...) {
  // BUG 489: The actual argument is a long but the iterator requests int;
  // the values are read with the wrong width and interpreted incorrectly.
  va_list ap;
  va_start(ap, fmt);
  long value = va_arg(ap, int);
  (void)value;
  va_end(ap);
  // SAFE FIX: use the exact promoted type that was passed (va_arg(ap, long)).
}

/* ========================================================================== */
void bug490_va_copy_not_ended(const char *fmt, ...) {
  // BUG 490: A va_copy is created but never ended, so its resources remain
  // live after the function returns.
  va_list ap, copy;
  va_start(ap, fmt);
  va_copy(copy, ap);
  vprintf(fmt, copy);
  va_end(ap);
  // SAFE FIX: pair every va_copy with its own va_end(copy).
}

/* ========================================================================== */
static void log_without_attribute(const char *fmt, ...) {
  // This wrapper forwards its format but carries no format attribute.
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

void bug491_wrapper_missing_format_attribute(void) {
  // BUG 491: Because the wrapper has no __attribute__((format)), the compiler
  // cannot check the argument list; a %d paired with a string goes unnoticed.
  log_without_attribute("%s %d", "text");
  // SAFE FIX: declare the wrapper with
  // __attribute__((format(printf, 1, 2))) so calls are checked.
}

/* ========================================================================== */
static void log_no_arg_check(const char *fmt, ...)
    __attribute__((format(printf, 1, 0)));
static void log_no_arg_check(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

void bug492_format_attribute_zero_index(void) {
  // BUG 492: Supplying 0 as the "arguments to check" index disables argument
  // validation even though a format attribute appears to be present.
  log_no_arg_check("%d %s");
  // SAFE FIX: use the real first-vararg index, e.g. format(printf, 1, 2).
}

/* ========================================================================== */
void bug493_tmpfile_handle_leak(void) {
  // BUG 493: The temporary stream is never closed, leaking the file
  // descriptor and its buffered data until process exit.
  FILE *tmp = tmpfile();
  if (tmp)
    fputc('x', tmp);
  // SAFE FIX: fclose(tmp) on every path after the write.
}

/* ========================================================================== */
void bug494_tmpfile_use_after_close(void) {
  // BUG 494: Writing through a FILE* after fclose is a use-after-free of the
  // stream object and may corrupt the allocator.
  FILE *tmp = tmpfile();
  if (!tmp)
    return;
  fclose(tmp);
  fputc('y', tmp);
  // SAFE FIX: set tmp = NULL immediately after closing and never reuse it.
}

int main(void) { return 0; }
