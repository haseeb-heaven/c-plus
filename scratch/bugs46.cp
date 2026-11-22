#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef __APPLE__
#include <mqueue.h>
#else
/* macOS has no POSIX message queues; minimal stand-ins keep the catalogue
 * compilable on the verification host without changing the bug being shown. */
typedef int mqd_t;
struct mq_attr {
  long mq_flags, mq_maxmsg, mq_msgsize, mq_curmsgs;
};
static mqd_t mq_open(const char *name, int oflag, ...) {
  (void)name; (void)oflag;
  return (mqd_t)-1;
}
static int mq_close(mqd_t d) { (void)d; return 0; }
static int mq_send(mqd_t d, const char *b, size_t n, unsigned p) {
  (void)d; (void)b; (void)n; (void)p;
  return 0;
}
#endif

// ============================================================================
// 725. Unnamed semaphore initialized but never destroyed
// ============================================================================
void bug725_sem_init_no_destroy(void) {
  sem_t s;
  sem_init(&s, 0, 1);
  // BUG 725: sem_init() allocates internal state that sem_destroy() releases;
  // abandoning the object leaks it.
  // SAFE FIX: call sem_destroy(&s) before s goes out of scope.
}

// ============================================================================
// 726. Named semaphore opened but never closed
// ============================================================================
void bug726_sem_open_no_close(const char *name) {
  sem_t *s = sem_open(name, O_CREAT, 0644, 1);
  if (s != SEM_FAILED) {
    // BUG 726: the process-wide named semaphore handle is never sem_close()'d.
    // SAFE FIX: sem_close(s) on every path after a successful sem_open().
  }
}

// ============================================================================
// 727. sem_t copied by value (its internal state is not copyable)
// ============================================================================
void bug727_sem_t_copied_by_value(void) {
  sem_t s = {0};
  sem_init(&s, 0, 1);
  // BUG 727: assigning the semaphore object by value duplicates opaque
  // waiter/owner state; both objects become invalid.
  sem_t copy = s;
  sem_post(&copy);
  sem_destroy(&s);
  // SAFE FIX: never copy sem_t; pass &s to functions that need it.
}

// ============================================================================
// 728. mq_open() descriptor leaked
// ============================================================================
void bug728_mq_open_no_close(const char *name) {
  mqd_t q = mq_open(name, O_CREAT | O_WRONLY, 0644, NULL);
  if (q != (mqd_t)-1) {
    mq_send(q, "x", 1, 0);
    // BUG 728: the message queue descriptor is never mq_close()'d.
    // SAFE FIX: call mq_close(q).
  }
}

// ============================================================================
// 729. mqd_t descriptor copied by value and closed through the copy
// ============================================================================
void bug729_mqd_copied_by_value(mqd_t q) {
  // BUG 729: a descriptor is an owning handle; duplicating it by value and
  // closing both aliases double-closes the same underlying queue.
  mqd_t alias = q;
  mq_close(alias);
  mq_close(q);
  // SAFE FIX: use dup() if a second handle is truly required, then close each.
}

// ============================================================================
// 730. aio_read() issued on a stack buffer that dies before completion
// ============================================================================
void bug730_aio_stack_buffer_lifetime(int fd) {
  char buf[64];
  struct aiocb a;
  memset(&a, 0, sizeof(a));
  a.aio_fildes = fd;
  a.aio_buf = buf;
  a.aio_nbytes = sizeof(buf);
  // BUG 730: the operation completes after this function returns, but both
  // 'buf' and the control block 'a' are automatic variables.
  aio_read(&a);
  // SAFE FIX: keep the buffer and aiocb alive (static/heap) until aio_return().
}

// ============================================================================
// 731. aio operation never awaited/cancelled on an error path
// ============================================================================
void bug731_aio_no_wait_on_error(int fd) {
  static char buf[64];
  static struct aiocb a;
  memset(&a, 0, sizeof(a));
  a.aio_fildes = fd;
  a.aio_buf = buf;
  a.aio_nbytes = sizeof(buf);
  if (aio_read(&a) == -1)
    return;
  // BUG 731: on the failure path the control block is discarded without
  // aio_cancel()/aio_error(); the completion state is lost.
  // SAFE FIX: always reap with aio_return()/aio_error() or aio_cancel().
}

// ============================================================================
// 732. socket descriptor not closed on an error path
// ============================================================================
int bug732_socket_no_close_on_error(void) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
    // BUG 732: early return abandons 'fd'; the descriptor leaks on this branch.
    return -1;
    // SAFE FIX: close(fd) before every error return.
  return fd;
}

// ============================================================================
// 733. dup()'d descriptor closed twice
// ============================================================================
void bug733_dup_fd_double_close(int fd) {
  int copy = dup(fd);
  if (copy >= 0) {
    close(copy);
    // BUG 733: a second close of the same duplicated descriptor can close an
    // unrelated fd if the number was reused in between.
    close(copy);
    // SAFE FIX: close(copy) exactly once and set copy = -1 afterwards.
  }
}

// ============================================================================
// 734. pipe read end leaked on the write-only path
// ============================================================================
void bug734_pipe_read_end_leaked(void) {
  int fds[2];
  if (pipe(fds) == 0) {
    // BUG 734: only the write end is used and closed; the read end stays open
    // so readers never see EOF.
    close(fds[1]);
    // SAFE FIX: close(fds[0]) as well when the read end is not needed.
  }
}

int main(void) { return 0; }
