/*===-- test_runtime_safety.c - runtime concurrency/regression tests --------===
 *
 * Standalone harness linked directly against runtime/cplus_runtime.c.  It
 * covers the four Agent 1 runtime findings:
 *   #72 - concurrent print() must not race on shared realloc buffers, and a
 *         throw must unwind print nesting depth back to the try frame.
 *   #73 - allocate/release churn must rehash the live table in place instead
 *         of doubling it on every tombstone cleanup cycle.
 *   #75 - mutex/TLS primitives must actually work on hosts with threads.
 *   #76/#110 - realloc must validate its source before allocating and perform
 *         its whole validate/copy/retire/insert/free transfer in one critical
 *         section; concurrent alloc/realloc/release churn must leave a
 *         consistent live table and keep errno/ABI semantics intact.
 *
 * Every check is deterministic; failures are counted and reported at exit.
 *
 *===----------------------------------------------------------------------===*/

#include "cplus/cplus.h"

#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "  FAIL %s (line %d)\n", (msg), __LINE__);               \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

/* --- stdout capture ------------------------------------------------------- */

typedef struct {
  int saved_fd;
  FILE *file;
} capture;

static int capture_start(capture *c, const char *path) {
  c->saved_fd = dup(STDOUT_FILENO);
  c->file = fopen(path, "w+");
  if (c->saved_fd < 0 || !c->file)
    return -1;
  fflush(stdout);
  dup2(fileno(c->file), STDOUT_FILENO);
  return 0;
}

static void capture_stop(capture *c) {
  fflush(stdout);
  dup2(c->saved_fd, STDOUT_FILENO);
  close(c->saved_fd);
  rewind(c->file);
}

/* --- #72: concurrent print() ---------------------------------------------- */

#define THREADS 4
#define LINES_PER_THREAD 200
#define PAYLOAD_LEN 220

static void *print_worker(void *arg) {
  int id = *(int *)arg;
  char tag[8];
  snprintf(tag, sizeof(tag), "T%d", id);
  char payload[PAYLOAD_LEN + 1];
  memset(payload, 'a' + id, PAYLOAD_LEN);
  payload[PAYLOAD_LEN] = '\0';
  /* Long payloads force repeated realloc of the per-thread buffer. */
  for (int i = 0; i < LINES_PER_THREAD; ++i)
    print(tag, id, i, payload);
  return NULL;
}

static void test_print_concurrency(void) {
  const char *path = "build/runtime/print_concurrent.out";
  capture cap;
  if (capture_start(&cap, path) != 0) {
    CHECK(0, "print concurrency: cannot capture stdout");
    return;
  }
  pthread_t threads[THREADS];
  int ids[THREADS];
  for (int i = 0; i < THREADS; ++i) {
    ids[i] = i;
    CHECK(pthread_create(&threads[i], NULL, print_worker, &ids[i]) == 0,
          "print concurrency: pthread_create");
  }
  for (int i = 0; i < THREADS; ++i)
    pthread_join(threads[i], NULL);
  capture_stop(&cap);

  int counts[THREADS] = {0};
  int bad = 0, total = 0;
  char line[1024];
  while (fgets(line, sizeof(line), cap.file)) {
    ++total;
    size_t len = strlen(line);
    if (len == 0 || line[len - 1] != '\n') {
      ++bad;
      continue;
    }
    line[len - 1] = '\0';
    int tag_id = -1, id = -1, idx = -1, payload_off = 0;
    if (sscanf(line, "T%d %d %d %n", &tag_id, &id, &idx, &payload_off) != 3) {
      ++bad;
      continue;
    }
    if (tag_id != id || id < 0 || id >= THREADS || idx < 0 ||
        idx >= LINES_PER_THREAD) {
      ++bad;
      continue;
    }
    const char *payload = line + payload_off;
    if (strlen(payload) != (size_t)PAYLOAD_LEN) {
      ++bad;
      continue;
    }
    for (int k = 0; k < PAYLOAD_LEN; ++k) {
      if (payload[k] != (char)('a' + id)) {
        ++bad;
        break;
      }
    }
    ++counts[id];
  }
  fclose(cap.file);
  CHECK(bad == 0, "print concurrency: corrupted/interleaved line");
  CHECK(total == THREADS * LINES_PER_THREAD,
        "print concurrency: wrong number of lines");
  for (int i = 0; i < THREADS; ++i)
    CHECK(counts[i] == LINES_PER_THREAD, "print concurrency: missing lines");
}

/* --- #72: print depth unwinds across throw -------------------------------- */

static void test_print_unwind(void) {
  const char *path = "build/runtime/print_unwind.out";
  capture cap;
  if (capture_start(&cap, path) != 0) {
    CHECK(0, "print unwind: cannot capture stdout");
    return;
  }
  cplus_print_begin(); /* depth 1 */
  cplus_print_value(CPLUS_PRINT_STR, "outer");
  CPlusExceptionFrame frame = {0};
  if (setjmp(frame.env) == 0) {
    cplus_exception_enter(&frame);
    cplus_print_begin(); /* depth 2 - abandoned by the throw */
    cplus_print_value(CPLUS_PRINT_STR, "lost");
    cplus_exception_raise(1, "boom", __FILE__, __LINE__);
    CHECK(0, "print unwind: raise returned");
  } else {
    /* Must append to the still-open outer print, not a leaked slot. */
    cplus_print_value(CPLUS_PRINT_STR, "after");
    cplus_print_end();
  }
  print("recovered");
  capture_stop(&cap);

  char line[256];
  if (fgets(line, sizeof(line), cap.file))
    CHECK(strcmp(line, "outer after\n") == 0,
          "print unwind: outer print lost across throw");
  else
    CHECK(0, "print unwind: missing first line");
  if (fgets(line, sizeof(line), cap.file))
    CHECK(strcmp(line, "recovered\n") == 0,
          "print unwind: depth was not restored");
  else
    CHECK(0, "print unwind: missing second line");
  fclose(cap.file);
}

/* --- #73: live table stays bounded under tombstone churn ------------------ */

#define CHURN_BATCH 16
#define CHURN_ROUNDS 200

static void test_live_churn_bounded(void) {
  size_t cap_before = 0, live_before = 0, del_before = 0;
  cplus_live_table_stats(&cap_before, &live_before, &del_before);
  CHECK(live_before == 0, "churn: unexpected live slots before test");

  /* Simple reuse loop: malloc hands back the same address, so insert() can
   * reclaim the tombstone directly.  This alone must also stay bounded. */
  for (int i = 0; i < 20000; ++i) {
    void *p = cplus_alloc(16);
    cplus_release(p);
  }

  /* The real trigger: use a fresh address range every round.  Unmanaged
   * "poison" blocks hold each round's freed addresses out of malloc's reuse
   * pool, so tombstones accumulate while at most CHURN_BATCH (< cap/2)
   * objects are live at a time.  The old runtime doubled the table on every
   * threshold crossing here, reaching thousands of slots. */
  void *poison[CHURN_ROUNDS][CHURN_BATCH];
  for (int r = 0; r < CHURN_ROUNDS; ++r) {
    void *p[CHURN_BATCH];
    for (int i = 0; i < CHURN_BATCH; ++i)
      p[i] = cplus_alloc(64);
    for (int i = 0; i < CHURN_BATCH; ++i)
      cplus_release(p[i]);
    for (int i = 0; i < CHURN_BATCH; ++i)
      poison[r][i] = malloc(64);
  }
  size_t cap = 0, live = 0, del = 0;
  cplus_live_table_stats(&cap, &live, &del);
  CHECK(live == 0, "churn: live count grew");
  CHECK(cap == (cap_before ? cap_before : 64u),
        "churn: live table doubled on tombstone cleanup");
  CHECK(del < cap, "churn: tombstones not purged");
  for (int r = 0; r < CHURN_ROUNDS; ++r)
    for (int i = 0; i < CHURN_BATCH; ++i)
      free(poison[r][i]);

  /* Genuine live-set growth must still grow the table. */
  void *ptrs[300];
  for (int i = 0; i < 300; ++i)
    ptrs[i] = cplus_alloc(8);
  cplus_live_table_stats(&cap, &live, &del);
  CHECK(live == 300, "growth: wrong live count");
  CHECK(cap >= 512, "growth: table did not grow for a real live set");
  for (int i = 0; i < 300; ++i)
    cplus_release(ptrs[i]);
  cplus_live_table_stats(&cap, &live, &del);
  CHECK(live == 0, "growth: live count after release");
}

/* --- #75: working mutex and TLS ------------------------------------------- */

static void *g_mutex = NULL;
static void *g_tls = NULL;
static volatile long g_counter = 0;

static void *sync_worker(void *arg) {
  long id = (long)arg;
  for (int i = 0; i < 2000; ++i) {
    cplus_mutex_lock(g_mutex);
    g_counter += 1;
    cplus_mutex_unlock(g_mutex);
  }
  cplus_tls_set(g_tls, (void *)(uintptr_t)(id + 1));
  void *seen = cplus_tls_get(g_tls);
  return seen == (void *)(uintptr_t)(id + 1) ? NULL : (void *)1;
}

static void test_mutex_tls(void) {
  g_mutex = cplus_mutex_new();
  g_tls = cplus_tls_new();
  CHECK(g_mutex != NULL, "mutex_new returned NULL");
  CHECK(g_tls != NULL, "tls_new returned NULL");
  if (!g_mutex || !g_tls)
    return;
  cplus_tls_set(g_tls, (void *)(uintptr_t)999);
  pthread_t threads[THREADS];
  for (long i = 0; i < THREADS; ++i)
    CHECK(pthread_create(&threads[i], NULL, sync_worker, (void *)i) == 0,
          "sync: pthread_create");
  for (int i = 0; i < THREADS; ++i) {
    void *result = (void *)1;
    pthread_join(threads[i], &result);
    CHECK(result == NULL, "tls: thread saw another thread's value");
  }
  CHECK(g_counter == THREADS * 2000, "mutex: lost updates (no exclusion)");
  CHECK(cplus_tls_get(g_tls) == (void *)(uintptr_t)999,
        "tls: main thread value was clobbered");
  cplus_tls_free(g_tls);
  cplus_mutex_free(g_mutex);
}

/* --- #76: realloc transfer, validation, and concurrent churn -------------- */

static void test_realloc_transfer(void) {
  unsigned char expected[32];
  unsigned char *p = (unsigned char *)cplus_alloc(32);
  CHECK(p != NULL, "realloc: alloc failed");
  if (!p)
    return;
  for (int i = 0; i < 32; ++i)
    p[i] = expected[i] = (unsigned char)i;
  uint64_t gen_p = cplus_allocation_generation(p);
  uint64_t epoch = cplus_epoch();
  unsigned char *q = (unsigned char *)cplus_realloc(p, 64);
  CHECK(q != NULL && q != p, "realloc: no fresh block");
  if (!q)
    return;
  CHECK(memcmp(q, expected, sizeof(expected)) == 0,
        "realloc: payload not preserved");
  CHECK(cplus_alloc_bytes(q) == 64, "realloc: wrong new size");
  CHECK(cplus_allocation_generation(p) == 0, "realloc: old slot still live");
  CHECK(cplus_address_retired(p) != 0, "realloc: old address not retired");
  CHECK(cplus_allocation_generation(q) != gen_p,
        "realloc: generation was not refreshed");
  CHECK(cplus_epoch() > epoch, "realloc: no generation issued");
  cplus_release(q);
}

static uint64_t g_epoch_before_invalid;
static void hook_no_alloc(cplus_exit_code code, const char *kind) {
  (void)kind;
  /* The hook runs inside cplus_print_fatal before _Exit.  Exit 200 proves the
   * trap happened and that no allocation/generation was consumed first. */
  if (code == CPLUS_EXIT_TYPE_CONFUSION &&
      cplus_epoch() == g_epoch_before_invalid)
    _Exit(200);
  _Exit(201);
}

static void test_realloc_invalid_does_not_allocate(void) {
  fflush(stdout);
  pid_t pid = fork();
  if (pid < 0) {
    CHECK(0, "realloc invalid: fork failed");
    return;
  }
  if (pid == 0) {
    g_epoch_before_invalid = cplus_epoch();
    cplus_fatal_hook = hook_no_alloc;
    cplus_realloc((void *)(uintptr_t)0x1234, 64);
    _Exit(0); /* must be unreachable */
  }
  int status = 0;
  if (waitpid(pid, &status, 0) != pid) {
    CHECK(0, "realloc invalid: waitpid failed");
    return;
  }
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 200,
        "realloc invalid: allocated before validating ptr (or did not trap)");
}

/* #110: the lock-gap race involved cplus_release() being called by another
 * thread after a realloc had copied but before it retired ptr.  Interleave
 * independent alloc/release churn with realloc workers so every possible
 * ordering of the three operations is exercised under the same allocator
 * lock.  A spurious double-free trap or a leaked live slot fails the test. */
static void *alloc_release_worker(void *arg) {
  long id = (long)arg;
  for (int i = 0; i < 3000; ++i) {
    size_t sz = (size_t)((i + (int)id) % 5 + 1) * 16u;
    unsigned char *p = (unsigned char *)cplus_alloc(sz);
    if (!p)
      return (void *)1;
    p[0] = (unsigned char)id;
    p[sz - 1] = (unsigned char)i;
    cplus_release(p);
  }
  return NULL;
}

static void *realloc_worker(void *arg) {
  long id = (long)arg;
  unsigned char *p = (unsigned char *)cplus_alloc(16);
  if (!p)
    return (void *)1;
  for (int i = 0; i < 2000; ++i) {
    size_t sz = (i & 1) ? 16 : 96;
    unsigned char *q = (unsigned char *)cplus_realloc(p, sz);
    if (!q)
      return (void *)1;
    p = q;
    p[0] = (unsigned char)id;
    p[sz - 1] = (unsigned char)(id ^ 0xff);
  }
  cplus_release(p);
  return NULL;
}

static void test_realloc_concurrency(void) {
  size_t cap_before = 0, live_before = 0, del_before = 0;
  cplus_live_table_stats(&cap_before, &live_before, &del_before);
  CHECK(live_before == 0, "realloc concurrency: live slots left by earlier test");
  pthread_t threads[THREADS];
  for (long i = 0; i < THREADS; ++i) {
    void *(*worker)(void *) = (i & 1) ? alloc_release_worker : realloc_worker;
    CHECK(pthread_create(&threads[i], NULL, worker, (void *)i) == 0,
          "realloc concurrency: pthread_create");
  }
  for (int i = 0; i < THREADS; ++i) {
    void *result = (void *)1;
    pthread_join(threads[i], &result);
    CHECK(result == NULL, "realloc concurrency: worker failed");
  }
  size_t cap = 0, live = 0, del = 0;
  cplus_live_table_stats(&cap, &live, &del);
  CHECK(live == 0, "realloc concurrency: leaked live slots");
  CHECK(cap == cap_before,
        "realloc concurrency: live table grew on tombstone churn");
}

/* ABI/errno preservation: cplus_realloc keeps the plain void* signature and
 * leaves errno untouched on success (malloc still reports ENOMEM on failure). */
static void test_realloc_errno(void) {
  unsigned char *p = (unsigned char *)cplus_alloc(32);
  CHECK(p != NULL, "realloc errno: alloc failed");
  if (!p)
    return;
  errno = EDOM;
  unsigned char *q = (unsigned char *)cplus_realloc(p, 64);
  CHECK(q != NULL, "realloc errno: realloc failed");
  CHECK(errno == EDOM, "realloc errno: success path clobbered errno");
  cplus_release(q);
}

int main(void) {
  test_print_concurrency();
  test_print_unwind();
  test_live_churn_bounded();
  test_mutex_tls();
  test_realloc_transfer();
  test_realloc_invalid_does_not_allocate();
  test_realloc_errno();
  test_realloc_concurrency();

  if (failures == 0) {
    printf("runtime_safety: ok\n");
    return 0;
  }
  printf("runtime_safety: %d check(s) failed\n", failures);
  return 1;
}
