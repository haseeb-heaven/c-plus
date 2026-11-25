#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

// ============================================================================
// 735. mmap() region never released with munmap()
// ============================================================================
void bug735_mmap_no_munmap(int fd) {
  void *p = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
  if (p != MAP_FAILED) {
    // BUG 735: the mapping stays in the process address space forever.
    // SAFE FIX: call munmap(p, 4096).
  }
}

// ============================================================================
// 736. munmap() called with a smaller length than was mapped
// ============================================================================
void bug736_munmap_wrong_length(int fd) {
  size_t len = 8192;
  void *p = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
  if (p != MAP_FAILED) {
    // BUG 736: only half the mapping is released; the tail stays mapped.
    munmap(p, len / 2);
    // SAFE FIX: munmap(p, len) with the exact original length.
  }
}

// ============================================================================
// 737. mmap() memory released with free() instead of munmap()
// ============================================================================
void bug737_mmap_freed_with_free(int fd) {
  void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  if (p != MAP_FAILED) {
    // BUG 737: free() on a mapping is undefined; it corrupts the malloc heap.
    free(p);
    // SAFE FIX: munmap(p, 4096).
  }
}

// ============================================================================
// 738. System V shared memory segment left attached
// ============================================================================
void bug738_shm_segment_left_attached(key_t key) {
  int id = shmget(key, 4096, IPC_CREAT | 0666);
  if (id >= 0) {
    void *p = shmat(id, NULL, 0);
    if (p != (void *)-1) {
      // BUG 738: the attaching process never shmdt()'s the segment; the shm
      // mapping is never unmapped from this process.
      (void)p;
      // SAFE FIX: call shmdt(p) when the segment is no longer used.
    }
  }
}

// ============================================================================
// 739. shmget() segment never marked for removal
// ============================================================================
void bug739_shmget_no_shmctl_rmid(key_t key) {
  int id = shmget(key, 4096, IPC_CREAT | 0666);
  if (id >= 0) {
    // BUG 739: without shmctl(id, IPC_RMID, NULL) the segment outlives all
    // processes and accumulates in the system.
    // SAFE FIX: mark the segment for destruction with IPC_RMID.
  }
}

// ============================================================================
// 740. dlopen() handle never released with dlclose()
// ============================================================================
void bug740_dlopen_no_dlclose(const char *path) {
  void *h = dlopen(path, RTLD_NOW);
  if (h) {
    // BUG 740: the shared object stays loaded and its constructors are never
    // balanced by dlclose().
    // SAFE FIX: call dlclose(h) when the module is no longer needed.
  }
}

// ============================================================================
// 741. dlclose() before a symbol obtained from dlsym() is used
// ============================================================================
int bug741_dlclose_before_symbol_use(const char *path) {
  void *h = dlopen(path, RTLD_NOW);
  if (!h)
    return -1;
  int (*fn)(void) = (int (*)(void))dlsym(h, "entry");
  dlclose(h);
  // BUG 741: fn points into code that dlclose() may have unmapped; calling it
  // is a jump into unmapped memory.
  int r = fn ? fn() : -1;
  // SAFE FIX: call the symbol before dlclose(h), or keep the handle open.
  return r;
}

// ============================================================================
// 742. dlclose() called on a failed (NULL) dlopen() handle
// ============================================================================
void bug742_dlclose_on_failed_handle(const char *path) {
  void *h = dlopen(path, RTLD_NOW);
  if (h == NULL) {
    // BUG 742: dlclose(NULL) is undefined and may crash.
    dlclose(h);
    // SAFE FIX: only dlclose a non-NULL handle from a successful dlopen().
  } else {
    dlclose(h);
  }
}

// ============================================================================
// 743. tempnam() name used later (TOCTOU between name creation and open)
// ============================================================================
void bug743_tempnam_toctou(void) {
  char *path = tempnam("/tmp", "igi");
  if (path) {
    // BUG 743: tempnam() returns only a name; an attacker can create or
    // symlink that path before open(), so this is a time-of-check/time-of-use
    // race and a possible symlink attack.
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (fd >= 0)
      close(fd);
    free(path);
    // SAFE FIX: use mkstemp(), which creates the file atomically.
  }
}

// ============================================================================
// 744. mkstemp() file never unlinked
// ============================================================================
void bug744_mkstemp_no_unlink(void) {
  char tmpl[] = "/tmp/igi-XXXXXX";
  int fd = mkstemp(tmpl);
  if (fd >= 0) {
    // BUG 744: closing the descriptor does not remove the directory entry;
    // the temp file (and its contents) persists indefinitely.
    close(fd);
    // SAFE FIX: unlink(tmpl) after creating/opening the file.
  }
}

int main(void) { return 0; }
