#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

// ============================================================================
// 705. Closing the fd returned by dirfd() and then calling closedir()
// ============================================================================
void bug705_dirfd_close_then_closedir(void) {
  DIR *d = opendir(".");
  if (d) {
    int fd = dirfd(d);
    // BUG 705: dirfd() does not transfer ownership; close(fd) releases the
    // same descriptor that closedir() will try to release again.
    close(fd);
    closedir(d);
    // SAFE FIX: never close(dirfd(d)); call closedir(d) only.
  }
}

// ============================================================================
// 706. opendir() result leaked on the success path
// ============================================================================
void bug706_opendir_no_closedir(void) {
  DIR *d = opendir("/tmp");
  if (d) {
    struct dirent *e = readdir(d);
    // BUG 706: DIR* is never released; the directory fd leaks.
    (void)e;
    // SAFE FIX: call closedir(d) before every return path.
  }
}

// ============================================================================
// 707. Reassigning a DIR* without closing the previous directory stream
// ============================================================================
void bug707_opendir_reassign_leak(void) {
  DIR *d = opendir("/tmp");
  // BUG 707: the first stream is overwritten and can no longer be closed.
  d = opendir("/var");
  if (d)
    closedir(d);
  // SAFE FIX: closedir(d) before assigning opendir() again.
}

// ============================================================================
// 708. fdopendir() transfers the fd, but caller closes it as well
// ============================================================================
void bug708_fdopendir_fd_double_close(int fd) {
  DIR *d = fdopendir(fd);
  if (d) {
    // BUG 708: fdopendir() owns fd on success; this close() is a second close
    // of a descriptor the DIR stream also closes in closedir().
    close(fd);
    closedir(d);
    // SAFE FIX: on success only closedir(d); on failure close(fd).
  } else {
    close(fd);
  }
}

// ============================================================================
// 709. Calling dirfd() after closedir() has invalidated the DIR object
// ============================================================================
int bug709_dirfd_after_closedir(void) {
  DIR *d = opendir(".");
  int fd = -1;
  if (d) {
    closedir(d);
    // BUG 709: d is dangling; dirfd(d) reads freed DIR state and the returned
    // fd is meaningless (and may already have been reused).
    fd = dirfd(d);
    // SAFE FIX: capture dirfd(d) BEFORE closedir(), or keep the DIR open.
  }
  return fd;
}

// ============================================================================
// 710. popen() stream closed with fclose() instead of pclose()
// ============================================================================
void bug710_popen_fclose_not_pclose(void) {
  FILE *f = popen("echo hi", "r");
  if (f) {
    char buf[32];
    if (fgets(buf, sizeof(buf), f))
      puts(buf);
    // BUG 710: fclose() does not waitpid() the child; it becomes a zombie and
    // the pipe stream is torn down without reaping.
    fclose(f);
    // SAFE FIX: use pclose(f).
  }
}

// ============================================================================
// 711. pclose() called twice on the same popen() stream
// ============================================================================
void bug711_pclose_twice(void) {
  FILE *f = popen("true", "r");
  if (f) {
    pclose(f);
    // BUG 711: the FILE* is already released; a second pclose() reaps an
    // unrelated pid or operates on freed FILE state.
    pclose(f);
    // SAFE FIX: set f = NULL after pclose() and never pclose it again.
  }
}

// ============================================================================
// 712. Ignoring fwrite() failures when writing to a popen() pipe
// ============================================================================
void bug712_popen_unchecked_fwrite(const char *data, size_t len) {
  FILE *f = popen("cat > /dev/null", "w");
  if (f) {
    // BUG 712: the return value is discarded.  If the child exits early the
    // write fails (SIGPIPE / short write) and the data is silently lost; the
    // stream is then released with fclose() instead of pclose().
    size_t written = fwrite(data, 1, len, f);
    (void)written;
    fclose(f);
    // SAFE FIX: check that fwrite() returns len and use pclose(f).
  }
}

// ============================================================================
// 713. tmpfile() stream never closed
// ============================================================================
void bug713_tmpfile_no_fclose(void) {
  FILE *f = tmpfile();
  if (f) {
    fputs("scratch", f);
    // BUG 713: temporary FILE* is abandoned; fclose() would unlink the file.
    // SAFE FIX: fclose(f) on all paths.
  }
}

// ============================================================================
// 714. fdopen() wrapper leaked while the original descriptor is reused
// ============================================================================
void bug714_fdopen_no_fclose(int fd) {
  FILE *f = fdopen(fd, "r");
  if (f) {
    // BUG 714: the FILE* owns fd now; returning without fclose(f) leaks the
    // buffered stream and the descriptor.
    return;
    // SAFE FIX: fclose(f) before returning (and do not close(fd) separately).
  }
}

int main(void) { return 0; }
