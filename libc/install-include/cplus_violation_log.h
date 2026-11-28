/* Bounded, opt-in diagnostic log for runtime safety violations.
 *
 * These APIs are ADDITIVE: they do not alter the existing fatal/trap ABI
 * (cplus_fatal, cplus_check_bounds/nonnull, cplus_runtime_violation,
 * cplus_runtime_get_violation_trap/get_failstop).  Nothing is opened or
 * written until the caller explicitly opens a log and supplies the path.
 *
 * Each event is appended as one fixed-format line:
 *
 *     <code> <kind> 0x<hex-pointer> <size>\n
 *
 * e.g. "101 bounds 0x1234 42\n".  `kind` is data, never a format string; it is
 * truncated to a fixed bound.  Writes are serialized by a mutex and performed
 * with a single O_APPEND write of a line smaller than PIPE_BUF, so lines are
 * not interleaved across threads.
 *
 * Link: build/libc/libcplus_hardened.a (with -pthread).
 */
#ifndef CPLUS_VIOLATION_LOG_H
#define CPLUS_VIOLATION_LOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Open (or replace) the log destination.  `path` must be a non-empty C string;
 * no path is hard-coded by the implementation.  Returns 0 on success, EINVAL
 * for a NULL/empty path, or the underlying open errno on failure.  The file is
 * opened in append mode; an existing file is never truncated. */
int cplus_violation_log_open(const char *path);

/* Append one diagnostic event.  Returns 0 on success; EINVAL for a NULL/empty
 * `kind`, EBADF when the log is not open, EFBIG when the bounded event cap is
 * reached, or an I/O errno on failure.  `kind` is never interpreted as a
 * format string.  `ptr` is rendered as lowercase hexadecimal. */
int cplus_violation_log_event(int code, const char *kind, const void *ptr,
                              size_t size);

/* Close the current log (no-op if none is open).  Writes are unbuffered, so
 * close() is the durability point; an explicit flush is not required. */
void cplus_violation_log_close(void);

/* Cumulative number of successfully written events since process start.
 * Refused or failed events are not counted.  Safe to call at any time. */
unsigned long cplus_violation_log_count(void);

#ifdef __cplusplus
}
#endif

#endif /* CPLUS_VIOLATION_LOG_H */
