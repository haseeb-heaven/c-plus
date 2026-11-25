c
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <iconv.h>
#include <mqueue.h>
#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>
#include <setjmp.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <wordexp.h>

    // ============================================================================
    // 205. getaddrinfo() Linked List Leaked (Missing freeaddrinfo)
    // ============================================================================
    void
    bug205_getaddrinfo_leak(void) {
  struct addrinfo hints = {0}, *res = NULL;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo("localhost", "80", &hints, &res) == 0) {
    // ERROR: Forgot freeaddrinfo(res); leaks entire linked list of addrinfo
    // structs
  }
}

// ============================================================================
// 206. Use-After-freeaddrinfo() on ai_addr / ai_canonname Pointer
// ============================================================================
void bug206_use_after_freeaddrinfo(void) {
  struct addrinfo hints = {0}, *res = NULL;
  hints.ai_flags = AI_CANONNAME;
  if (getaddrinfo("localhost", "80", &hints, &res) == 0) {
    struct sockaddr *saved_sa = res->ai_addr;
    freeaddrinfo(
        res); // Frees res and all nested ai_addr / ai_canonname allocations
    printf("family=%d\n",
           saved_sa->sa_family); // ERROR: Use-after-free reading saved_sa
  }
}

// ============================================================================
// 207. glob() Result Structure Leaked (Missing globfree)
// ============================================================================
void bug207_glob_leak(void) {
  glob_t g;
  if (glob("*.c", 0, NULL, &g) == 0) {
    // ERROR: Forgot globfree(&g); leaks g.gl_pathv array and all matched path
    // strings
  }
}

// ============================================================================
// 208. globfree() Called After Failed glob() Without GLOB_APPEND
// ============================================================================
void bug208_globfree_on_failure(void) {
  glob_t g; // Uninitialized fields
  if (glob("/nonexistent_dir_xyz/*", 0, NULL, &g) != 0) {
    globfree(&g); // ERROR: On GLOB_NOMATCH/error, gl_pathv may remain
                  // uninitialized garbage -> invalid free()
  }
}

// ============================================================================
// 209. wordexp() Expansion Leaked (Missing wordfree)
// ============================================================================
void bug209_wordexp_leak(void) {
  wordexp_t we;
  if (wordexp("$HOME/*.txt", &we, 0) == 0) {
    // ERROR: Forgot wordfree(&we); leaks expanded argument vector
  }
}

// ============================================================================
// 210. iconv_open() Conversion Descriptor Leaked (Missing iconv_close)
// ============================================================================
void bug210_iconv_leak(int fail_early) {
  iconv_t cd = iconv_open("UTF-8", "ISO-8859-1");
  if (cd == (iconv_t)-1)
    return;
  if (fail_early) {
    return; // ERROR: Leaks iconv conversion state descriptor without
            // iconv_close(cd)
  }
  iconv_close(cd);
}

// ============================================================================
// 211. Checking iconv_open() Against NULL Instead of (iconv_t)-1
// ============================================================================
void bug211_iconv_null_check(void) {
  iconv_t cd = iconv_open("INVALID_CHARSET_A",
                          "INVALID_CHARSET_B"); // Returns (iconv_t)-1
  if (cd != NULL) {  // ERROR: (iconv_t)-1 != NULL; passes check!
    iconv_close(cd); // ERROR: Passes (iconv_t)-1 to iconv_close -> segfault /
                     // invalid free
  }
}

// ============================================================================
// 212. Named POSIX Semaphore (sem_open) Leaked Without sem_close()
// ============================================================================
void bug212_sem_open_leak(void) {
  sem_t *sem = sem_open("/my_test_sem", O_CREAT, 0600, 1);
  if (sem != SEM_FAILED) {
    sem_wait(sem);
    sem_post(sem);
    sem_unlink("/my_test_sem");
    // ERROR: Forgot sem_close(sem); leaks per-process semaphore mapping and
    // descriptor
  }
}

// ============================================================================
// 213. Copying sem_t By Value (Undefined Behavior / Corrupts Internal State)
// ============================================================================
void bug213_copy_sem_t(void) {
  sem_t s1;
  sem_init(&s1, 0, 1);
  sem_t s2 = s1; // ERROR: POSIX forbids copying sem_t; operating on copy is
                 // undefined behavior
  sem_wait(&s2);
  sem_post(&s2);
  sem_destroy(&s1);
}

// ============================================================================
// 214. POSIX Message Queue (mq_open) Descriptor Leaked (Missing mq_close)
// ============================================================================
void bug214_mq_open_leak(void) {
  struct mq_attr attr = {.mq_maxmsg = 4, .mq_msgsize = 64};
  mqd_t mq = mq_open("/test_mq_bug", O_CREAT | O_RDWR, 0600, &attr);
  if (mq != (mqd_t)-1) {
    mq_unlink("/test_mq_bug");
    // ERROR: Forgot mq_close(mq); leaks message queue descriptor in process
  }
}

// ============================================================================
// 215. popen() Closed With fclose() Instead of pclose() (Zombie Process + Leak)
// ============================================================================
void bug215_popen_fclose_mismatch(void) {
  FILE *pipe = popen("echo hello", "r");
  if (pipe) {
    char buf;
    fgets(buf, sizeof(buf), pipe);
    fclose(pipe); // ERROR: Must use pclose(pipe); fclose() leaks child PID
                  // tracking and leaves zombie process
  }
}

// ============================================================================
// 216. fopen() Closed With pclose() (Undefined Behavior / waitpid on Invalid
// Stream)
// ============================================================================
void bug216_fopen_pclose_mismatch(void) {
  FILE *f = fopen("/dev/null", "r");
  if (f) {
    pclose(
        f); // ERROR: Passing non-popen FILE* to pclose() is undefined behavior
  }
}

// ============================================================================
// 217. getsockopt() / accept() optlen/addrlen Uninitialized Before Call
// ============================================================================
void bug217_uninit_socklen(int sockfd) {
  struct sockaddr_in client_addr;
  socklen_t addrlen; // ERROR: Uninitialized! Kernel treats garbage value as
                     // buffer capacity -> stack overflow!
  int cfd = accept(sockfd, (struct sockaddr *)&client_addr, &addrlen);
  if (cfd >= 0)
    close(cfd);
}

// ============================================================================
// 218. sockaddr_un sun_path Not Null-Terminated Before strlen/printf
// ============================================================================
void bug218_sockaddr_un_no_null(void) {
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  // Linux allows filling all 108 bytes of sun_path without '\0'
  memset(addr.sun_path, 'a', sizeof(addr.sun_path));
  printf("Socket path: %s\n",
         addr.sun_path); // ERROR: Over-reads past sockaddr_un struct because no
                         // '\0' exists
}

// ============================================================================
// 219. inet_ntoa() Static Buffer Overwritten on Second Call in Same Expression
// ============================================================================
void bug219_inet_ntoa_static_alias(void) {
  struct in_addr src = {.s_addr = htonl(0x7F000001)};
  struct in_addr dst = {.s_addr = htonl(0x08080808)};
  // ERROR: Both inet_ntoa() calls return the exact same static char* buffer!
  printf("src=%s dst=%s\n", inet_ntoa(src), inet_ntoa(dst));
}

// ============================================================================
// 220. Passing Unaligned Buffer to recvmsg() Control Message (CMSG_FIRSTHDR)
// ============================================================================
void bug220_cmsg_unaligned_buffer(int sockfd) {
  char
      ctrl_buf; // ERROR: Plain char[] is not aligned to alignof(struct cmsghdr)
  struct iovec iov = {.iov_base = ctrl_buf, .iov_len = 1};
  struct msghdr msg = {0};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = ctrl_buf; // ERROR: CMSG_NXTHDR/recvmsg accesses misaligned
                              // cmsghdr fields -> UB / SIGBUS
  msg.msg_controllen = sizeof(ctrl_buf);
  recvmsg(sockfd, &msg, 0);
}

// ============================================================================
// 221. Custom Pool/Slab Allocator Double-Initialization Clobbering Live
// Allocations
// ============================================================================
struct Bug221Slab {
  uint8_t *mem;
  size_t used;
};
void bug221_slab_reinit_leak(struct Bug221Slab *s) {
  s->mem = (uint8_t *)malloc(4096);
  s->used = 0;
}
void bug221_double_init_leak(void) {
  struct Bug221Slab s = {0};
  bug221_slab_reinit_leak(&s);
  bug221_slab_reinit_leak(&s); // ERROR: Re-initializing without destroying
                               // leaks first 4096-byte slab
  free(s.mem);
}

// ============================================================================
// 222. Intrusive Doubly-Linked List Self-Insertion Corrupting Pointers
// (Infinite Loop / UAF)
// ============================================================================
struct Bug222Link {
  struct Bug222Link *prev, *next;
};
void bug222_insert_after(struct Bug222Link *pos, struct Bug222Link *node) {
  Use code with caution
      .
      // ERROR: If pos == node (inserting node after itself), node->next becomes
      // node!
      node->next = pos->next;
  node->prev = pos;
  if (pos->next)
    pos->next->prev = node;
  pos->next = node;
}
void bug222_self_link_corruption(void) {
  struct Bug222Link *n =
      (struct Bug222Link *)calloc(1, sizeof(struct Bug222Link));
  bug222_insert_after(n, n); // Creates self-cycle n->next == n
  free(n);
}
// ============================================================================
// 223. B-Tree / Dynamic Array Split Failing to Zero Old Tail Slots (Dangling
// Refs)
// ============================================================================
void bug223_split_stale_tail_pointers(void) {
  char **items = (char **)malloc(4 * sizeof(char *));
  for (int i = 0; i < 4; i++)
    items[i] = strdup("entry");
  size_t count = 4;
  // Move upper half [2..3] to another owner, shrink count to 2, leave stale
  // pointers in items..
  char **other = (char **)malloc(2 * sizeof(char *));
  other = items;
  other = items;
  count = 2;
  // Later full cleanup frees up to capacity (4) instead of count (2), or
  // re-uses unzeroed slots:
  free(other);
  free(other);
  free(other);
  for (size_t i = 0; i < 4;
       i++) { // ERROR: Frees items and items a second time!
    free(items[i]);
  }
  free(items);
}
// ============================================================================
// 224. Union Active Member Type-Punning Violation in C (Reading Inactive
// Pointer Field)
// ============================================================================
union Bug224Val {
  uintptr_t raw_bits;
  int *ptr;
};
void bug224_union_pointer_pun(void) {
  union Bug224Val v;
  v.raw_bits = 0x12345678ULL;
  // Dereferencing fabricated address from integer union member
  int x =
      *v.ptr; // ERROR: Dereferencing non-existent object address -> segfault
  (void)x;
}
// ============================================================================
// 225. Flexible Array Member With Non-Zero Initializer Undefined / Stack OOB
// ============================================================================
struct Bug225Flex {
  int len;
  char data[];
};
void bug225_stack_fam_access(void) {
  struct Bug225Flex f = {.len =
                             4}; // sizeof(f) == sizeof(int), data has 0 bytes!
  f.data = 'A'; // ERROR: Stack buffer overflow writing into flexible array
                // member on automatic variable
}
// ============================================================================
// 226. Allocating Array With sizeof(struct) Without Accounting for Trailing
// Padding
// ============================================================================
struct Bug226Padded {
  int a;
  char b;
}; // sizeof == 8 (includes 3 bytes trailing padding)
void bug226_underalloc_without_padding(void) {
  // Manual sum of field sizes (4 + 1 = 5) instead of sizeof(struct
  // Bug226Padded) (8)
  size_t bad_stride = sizeof(int) + sizeof(char);
  struct Bug226Padded *arr = (struct Bug226Padded *)malloc(
      2 * bad_stride); // Allocates 10 bytes instead of 16!
  arr.a = 1;
  arr.b = 'x'; // ERROR: Writes at offset 12 inside 10-byte allocation (heap
               // buffer overflow)
  free(arr);
}
// ============================================================================
// 227. Pointer Comparison Against One-Past-End Using >= Instead of == With
// Unrelated Array
// ============================================================================
void bug227_cross_array_loop_bound(void) {
  int a = {1, 2, 3, 4};
  int b = {5, 6, 7, 8};
  // Compiler may place 'b' immediately after 'a' on stack
  for (int *p = a; p != b; p++) { // ERROR: Comparing pointers from distinct
                                  // arrays 'a' and 'b' is UB
    *p = 0;
  }
}
// ============================================================================
// 228. Restrict-Qualified Struct Member Violated via Struct Copy
// ============================================================================
struct Bug228Slice {
  int *restrict data;
  size_t len;
};
void bug228_restrict_struct_alias(void) {
  int buf = {1, 2, 3, 4};
  struct Bug228Slice s1 = {buf, 4};
  struct Bug228Slice s2 =
      s1; // Copies restrict pointer into another active scope
  s1.data = 10;
  s2.data = 20; // ERROR: Aliasing modification through copied restrict pointer
                // violates restrict semantics
}
// ============================================================================
// 229. Const Pointer-to-Pointer Conversion Pitfall (Modifying Const Data
// Without Cast)
// ============================================================================
void bug229_const_double_ptr_bypass(void) {
  const int secret = 42;
  const int *cptr = &secret;
  // Simulating passing &cptr where const rules are bypassed via void*
  void *vp = &cptr;
  int **mpp = (int **)vp;
  **mpp = 99; // ERROR: Modifies 'const int secret' object -> undefined behavior
}
// ============================================================================
// 230. Returning Pointer Into VLA After Function Return
// ============================================================================
int *bug230_return_vla_ptr(int n) {
  int vla[n];
  vla = 123;
  return vla; // ERROR: VLA automatic storage ends on return; returns dangling
              // stack pointer
}
// ============================================================================
// 231. Thread-Safe Reference Counter Missing Acquire/Release Memory Ordering
// (Use-After-Free on Weak Arch)
// ============================================================================
struct Bug231Arc {
  atomic_int refs;
  int *data;
};
void bug231_arc_drop(struct Bug231Arc *arc) {
  // ERROR: memory_order_relaxed allows prior reads/writes to arc->data to
  // reorder AFTER atomic_fetch_sub, or another thread's free() to become
  // visible before this thread finishes reading arc->data!
  if (atomic_fetch_sub_explicit(&arc->refs, 1, memory_order_relaxed) == 1) {
    free(arc->data);
    free(arc);
  }
}
// ============================================================================
// 232. Spinlock Unlock Using Relaxed Store Allowing Critical Section Memory
// Leak/Race
// ============================================================================
atomic_flag bug232_lock = ATOMIC_FLAG_INIT;
int *bug232_shared_buf = NULL;
void bug232_relaxed_unlock_uaf(void) {
  while (
      atomic_flag_test_and_set_explicit(&bug232_lock, memory_order_acquire)) {
  }
  free(bug232_shared_buf);
  bug232_shared_buf = NULL;
  // ERROR: memory_order_relaxed does not publish stores before unlock; next
  // thread can read stale pointer
  atomic_flag_clear_explicit(&bug232_lock, memory_order_relaxed);
}
// ============================================================================
// 233. pthread_once() Init Routine Failing Without Setting Output Pointer (Null
// Deref)
// ============================================================================
pthread_once_t bug233_once = PTHREAD_ONCE_INIT;
int *bug233_singleton = NULL;
void bug233_init_fn(void) {
  // Simulates allocation failure; pthread_once marks itself "done" anyway and
  // will NEVER retry!
  bug233_singleton = (int *)malloc((size_t)-1); // NULL
}
void bug233_use_singleton(void) {
  pthread_once(&bug233_once, bug233_init_fn);
  *bug233_singleton =
      1; // ERROR: Dereferences NULL because failed init is never retried
}
// ============================================================================
// 234. Destroying pthread_rwlock_t While Read Lock Is Still Held
// ============================================================================
void bug234_destroy_held_rwlock(void) {
  pthread_rwlock_t *rw = (pthread_rwlock_t *)malloc(sizeof(pthread_rwlock_t));
  pthread_rwlock_init(rw, NULL);
  pthread_rwlock_rdlock(rw);
  pthread_rwlock_destroy(
      rw); // ERROR: Destroying rwlock while locked is undefined behavior
  free(rw);
}
// ============================================================================
// 235. Using fork() in Multi-Threaded Process and Calling malloc() in Child
// (Deadlock)
// ============================================================================
void bug235_fork_multithreaded_malloc(void) {
  // If another thread held glibc's malloc arena lock at the instant fork() ran,
  // that thread vanishes in the child, leaving the arena lock permanently
  // locked!
  pid_t pid = fork();
  if (pid == 0) {
    void *p = malloc(64); // ERROR: Async-signal-unsafe after multi-threaded
                          // fork() -> deadlocks child
    free(p);
    _exit(0);
  }
}
// ============================================================================
// 236. vfork() Child Modifying Heap / Calling free() or Returning From Function
// ============================================================================
void bug236_vfork_heap_corruption(void) {
  int *p = (int *)malloc(sizeof(int));
  pid_t pid = vfork();
  if (pid == 0) {
    // Child shares parent's exact address space and stack frame until
    // _exit/exec!
    free(p); // ERROR: Frees parent's heap memory and corrupts parent's
             // allocator state!
    _exit(0);
  }
  *p = 10; // ERROR: Parent writes to memory freed by vfork child
           // (use-after-free)
  free(p); // ERROR: Parent double-frees 'p'
}
// ============================================================================
// 237. sigaltstack() Alternate Signal Stack Freed Before Disabling Signal Stack
// ============================================================================
void bug237_sigaltstack_uaf(void) {
  stack_t ss;
  ss.ss_sp = malloc(SIGSTKSZ);
  ss.ss_size = SIGSTKSZ;
  ss.ss_flags = 0;
  sigaltstack(&ss, NULL);
  free(ss.ss_sp); // ERROR: Kernel still holds pointer to ss.ss_sp for
                  // SA_ONSTACK signal delivery!
}
// ============================================================================
// 238. Passing Stack Buffer to sigaction() / SA_ONSTACK Alternate Stack
// ============================================================================
void bug238_sigaltstack_dangling_stack(void) {
  char stack_mem[SIGSTKSZ];
  stack_t ss = {
      .ss_sp = stack_mem, .ss_size = sizeof(stack_mem), .ss_flags = 0};
  sigaltstack(&ss, NULL);
  // ERROR: Function returns and destroys stack_mem while kernel retains pointer
  // to it
}
// ============================================================================
// 239. Timer Handler (timer_create / SIGEV_THREAD) Accessing Freed Context
// After timer_delete
// ============================================================================
void bug239_timer_cb(union sigval sv) {
  int *counter = (int *)sv.sival_ptr;
  counter += 1; // ERROR: In-flight timer callback races with timer_delete() +
                // free(counter)
}
void bug239_posix_timer_uaf(void) {
  int ctx = (int *)calloc(1, sizeof(int));
  timer_t tid;
  struct sigevent sev = {0};
  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_notify_function = bug239_timer_cb;
  sev.sigev_value.sival_ptr = ctx;
  if (timer_create(CLOCK_MONOTONIC, &sev, &tid) == 0) {
    timer_delete(tid); // Does NOT wait for already-spawned SIGEV_THREAD
                       // callbacks to finish!
    free(ctx);         // ERROR: Active timer thread still dereferences 'ctx'
  }
}
// ============================================================================
// 240. String Format Vulnerability (%n Writing Arbitrary Address From User
// Input)
// ============================================================================
void bug240_format_string_write(const char *untrusted_input) {
  // ERROR: Passing untrusted string as format specifier allows %n to write to
  // stack pointers
  printf(untrusted_input);
}
// ============================================================================
// 241. fscanf() / sscanf() %[scanset] Without Width Limit (Buffer Overflow)
// ============================================================================
void bug241_scanset_overflow(void) {
  char token;
  sscanf("abcdefghijklmnopqrstuvwxyz", "%[a-z]",
         token); // ERROR: Writes 27 bytes into 8-byte buffer
}
// ============================================================================
// 242. strxfrm() Return Value Misinterpreted as Bytes Written (Truncated /
// Unterminated)
// ============================================================================
void bug242_strxfrm_no_null(void) {
  char dst;
  size_t req = strxfrm(dst, "long_locale_string", sizeof(dst));
  if (req >= sizeof(dst)) {
    // C standard states contents of 'dst' are indeterminate (and NOT
    // null-terminated) when req >= n!
    printf("%s\n",
           dst); // ERROR: Reading indeterminate, non-null-terminated buffer
  }
}
// ============================================================================
// 243. stpncpy() Return Pointer Used Without Checking If Buffer Was Filled (No
// '\0')
// ============================================================================
void bug243_stpncpy_missing_null(void) {
  char buf;
  char *end =
      stpncpy(buf, "abcdef",
              sizeof(buf)); // Fills all 4 bytes, writes NO '\0', returns &buf
  (void)end;
  printf("%s\n", buf); // ERROR: Over-reads past 'buf' because stpncpy omits
                       // '\0' when src >= n
}
// ============================================================================
// 244. memccpy() Return Value Ignored When Delimiter Not Found (Missing Null
// Terminator)
// ============================================================================
void bug244_memccpy_unterminated(void) {
  char dst;
  void *p =
      memccpy(dst, "abcdefgh", ':',
              sizeof(dst)); // ':' not found -> returns NULL, no '\0' written
  if (!p) {
    printf("%s\n", dst); // ERROR: dst is not null-terminated -> stack over-read
  }
}
// ============================================================================
// 245. reallocarray() Zero-Element Edge Case Dereference
// ============================================================================
void bug245_reallocarray_zero(void) {
  int *p = (int *)reallocarray(NULL, 0, sizeof(int));
  if (p) {
    *p = 1; // ERROR: Dereferencing 0-byte allocation is undefined behavior
    free(p);
  }
}
// ============================================================================
// 246. Aligned Allocation Size Underflow When Subtracting Header Size
// ============================================================================
void bug246_header_size_underflow(size_t requested_size) {
  // Suppose requested_size == 4, and header is 16 bytes
  size_t payload_cap =
      requested_size - 16; // ERROR: Unsigned underflow wraps to SIZE_MAX - 11!
  char *buf = (char *)malloc(requested_size);
  if (buf && payload_cap > 0) {
    memset(buf, 0,
           16); // ERROR: Writes 16 bytes into 4-byte buffer (heap overflow)
  }
  free(buf);
}
// ============================================================================
// 247. Off-By-One in Binary Search High Index Causing Out-of-Bounds Read
// ============================================================================
int bug247_bsearch_oob(const int *arr, size_t n, int target) {
  size_t lo = 0, hi = n; // 'hi' starts at n (one past end)
  while (lo <= hi) { // ERROR: Using '<=' instead of '<' allows mid == n when
                     // target > arr[n-1]!
    size_t mid = lo + (hi - lo) / 2;
    if (arr[mid] == target)
      return (int)mid; // ERROR: Reads arr[n] out-of-bounds
    if (arr[mid] < target)
      lo = mid + 1;
    else
      hi = mid - 1; // Also underflows when mid == 0!
  }
  return -1;
}
// ============================================================================
// 248. Circular Queue / Ring Buffer Pointer Wrap Comparison Using > Instead of
// ==
// ============================================================================
void bug248_ring_ptr_wrap_oob(void) {
  int buf;
  int *end = buf + 4;
  int *p = buf + 3;
  p += 2; // Jumps 2 elements forward -> p == buf + 5 (past one-past-end!)
  if (p == end)
    p = buf; // ERROR: Missed equality check; p is now out-of-bounds!
  *p = 7;    // ERROR: Stack buffer overflow
}
// ============================================================================
// 249. Custom Reference-Counted String Slice Outliving Backing Buffer
// ============================================================================
struct Bug249Slice {
  const char *ptr;
  size_t len;
};
struct Bug249Slice bug249_make_slice(void) {
  char *owned = strdup("temporary heap string");
  struct Bug249Slice s = {owned + 10, 4};
  free(owned); // Frees backing allocation before returning slice
  return s;    // ERROR: Returns dangling interior slice pointer
}
// ============================================================================
// 250. Double-Free When Custom Destructor Both Frees Member and Caller Frees
// Member
// ============================================================================
struct Bug250Buffer {
  void *data;
  void(free_fn)(void);
};
void bug250_destroy(struct Bug250Buffer *b) {
  if (b->free_fn)
    b->free_fn(b->data);
  free(b->data); // ERROR: Unconditionally calls free(b->data) even after custom
                 // free_fn already freed it!
}
// ============================================================================
// 251. Memory Leak When Overwriting Function Pointer Destructor With NULL
// ============================================================================
void bug251_cleared_destructor_leak(void) {
  struct Bug250Buffer b;
  b.data = malloc(64);
  b.free_fn = free;
  b.free_fn = NULL; // Caller clears callback before cleanup
  if (b.free_fn) {
    b.free_fn(
        b.data); // ERROR: Skipped because free_fn was nulled; leaks b.data
  }
}
// ============================================================================
// 252. VLA Size Determined From Unchecked File/Network Input (Arbitrary Stack
// Shift)
// ============================================================================
void bug252_untrusted_vla_size(size_t network_len) {
  // Even if network_len doesn't crash immediately, huge VLA skips stack guard
  // page into heap/BSS!
  char frame_buf[network_len]; // ERROR: Arbitrary stack pointer subtraction
                               // (Stack Clash vulnerability)
  frame_buf = 0;
}
// ============================================================================
// 253. Calling longjmp() Across Thread Boundaries (Corrupts Stack Pointer
// Across Threads)
// ============================================================================
jmp_buf bug253_cross_thread_env;
void *bug253_jump_thread(void *arg) {
  (void)arg;
  // ERROR: longjmp() into a jmp_buf captured by a different thread is
  // catastrophic UB
  longjmp(bug253_cross_thread_env, 1);
  return NULL;
}
// ============================================================================
// 254. Non-Volatile Automatic Variable Modified Between setjmp and longjmp
// ============================================================================
void bug254_nonvolatile_loop_counter_setjmp(void) {
  int count = 0; // ERROR: Must be 'volatile int count' if modified after setjmp
                 // and read after longjmp
  if (setjmp(bug253_cross_thread_env) == 0) {
    count++;
    longjmp(bug253_cross_thread_env, 1);
  }
  printf("count=%d\n", count); // ERROR: Indeterminate value (UB in C standard)
}
// ============================================================================
// 255. Signal Handler Modifying Non-sig_atomic_t Global Pointer (Torn Pointer
// Read)
// ============================================================================
int *bug255_global_ptr = NULL;
void bug255_handler(int sig) {
  (void)sig;
  bug255_global_ptr = NULL; // ERROR: Pointer is not sig_atomic_t; assignment
                            // can be torn across instructions
}
// ============================================================================
// 256. Re-Entrant Non-Recursive Mutex Deadlock in Signal Handler Freeing
// Resource
// ============================================================================
pthread_mutex_t bug256_mtx = PTHREAD_MUTEX_INITIALIZER;
void *bug256_res = NULL;
void bug256_sig_cleanup(int sig) {
  (void)sig;
  pthread_mutex_lock(&bug256_mtx); // ERROR: Deadlocks if signal interrupted
                                   // main thread while holding bug256_mtx
  free(bug256_res);
  bug256_res = NULL;
  pthread_mutex_unlock(&bug256_mtx);
}
// ============================================================================
// 257. Heap Spray / Uninitialized Pointer Array Scanned Until Sentinel NULL
// ============================================================================
void bug257_unterminated_ptr_array(void) {
  char **list = (char **)malloc(
      3 * sizeof(char *)); // ERROR: Forgot +1 slot for trailing NULL sentinel
  list = strdup("one");
  list = strdup("two");
  list = strdup("three");
  for (size_t i = 0; list[i] != NULL;
       i++) { // ERROR: Reads list out-of-bounds until hitting random NULL on
              // heap
    free(list[i]);
  }
  free(list);
}
// ============================================================================
// 258. Reallocating Pointer Array Without Updating Cached End Pointer
// ============================================================================
void bug258_stale_end_pointer(void) {
  int *begin = (int *)malloc(4 * sizeof(int));
  int *end = begin + 4;
  int *grown = (int *)realloc(
      begin, 8 * sizeof(int)); // Moves allocation to new address!
  if (grown) {
    begin = grown;
    // ERROR: Forgot to update 'end = begin + 8'; 'end' still points to old
    // freed block!
    for (int *p = begin; p != end;
         p++) { // ERROR: Compares pointers from two different allocations ->
                // infinite OOB loop!
      *p = 0;
    }
    free(begin);
  }
}
// ============================================================================
// 259. Custom Allocator Footer Canary Overwritten by Off-By-One String Copy
// ============================================================================
void bug259_canary_overwrite(void) {
  size_t user_len = 8;
  char *block = (char *)malloc(user_len + sizeof(uint32_t));
  (uint32_t)(block + user_len) =
      0xCAFEBABE; // Footer canary right after user payload
  memcpy(block, "123456789",
         9); // ERROR: 9-byte write corrupts 1st byte of footer canary!
  free(block);
}
// ============================================================================
// 260. Freeing Pointer Returned by gettext() / setlocale() / getenv()
// ============================================================================
#include <locale.h>
void bug260_free_setlocale_ptr(void) {
  char *loc =
      setlocale(LC_ALL, NULL); // Points to static/internal C library storage
  free(loc); // ERROR: Calling free() on pointer not obtained from
             // malloc/calloc/realloc -> heap corruption
}
// ============================================================================
// 261. Modifying String Returned by setlocale() in Place
// ============================================================================
void bug261_modify_setlocale_ptr(void) {
  char *loc = setlocale(LC_ALL, NULL);
  if (loc && loc) {
    loc = 'X'; // ERROR: Modifying library-owned static string causes segfault /
               // corrupts locale state
  }
}
// ============================================================================
// 262. Using strerror_r() GNU vs XSI Return Value Confusion (Integer Cast to
// char*)
// ============================================================================
void bug262_strerror_r_signature_confusion(void) {
  char buf;
  // With _GNU_SOURCE defined, strerror_r returns char* (which may be a static
  // string, NOT buf!)
  char *msg = strerror_r(EINVAL, buf, sizeof(buf));
  // Assuming result was always written into 'buf':
  printf("buf=%s msg=%s\n", buf,
         msg); // ERROR: 'buf' remains completely uninitialized if GNU
               // strerror_r returned static literal!
}
// ============================================================================
// 263. Passing Stack-Allocated epoll_event Array With Negative maxevents
// ============================================================================
void bug263_epoll_wait_invalid_count(int epfd) {
  struct epoll_event events;
  // Passing larger maxevents (16) than actual array length (4)
  int n = epoll_wait(
      epfd, events, 16,
      0); // ERROR: Kernel can write up to 16 events into 4-element stack array
  (void)n;
}
// ============================================================================
// 264. Using mremap() With MREMAP_MAYMOVE Without Updating Secondary Pointers
// ============================================================================
void bug264_mremap_dangling_alias(void) {
  char *map1 = (char *)mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (map1 == MAP_FAILED)
    return;
  char *cursor = map1 + 128;
  char *map2 = (char *)mremap(
      map1, 4096, 65536, MREMAP_MAYMOVE); // Unmaps old map1 and moves pages!
  if (map2 != MAP_FAILED) {
    *cursor = 'A'; // ERROR: 'cursor' still points into unmapped old virtual
                   // address range -> SIGSEGV
    munmap(map2, 65536);
  }
}
// ============================================================================
// 265. File Stream Buffer Read After fseek() Without fflush() on Update Mode
// ("r+")
// ============================================================================
void bug265_stdio_rw_switch_ub(void) {
  FILE *f = tmpfile();
  if (!f)
    return;
  fwrite("hello", 1, 5, f);
  // C standard forbids reading immediately after writing on an update stream
  // without intervening fflush/fseek!
  char ch = (char)fgetc(
      f); // ERROR: Undefined behavior on internal FILE buffer pointers
  (void)ch;
  fclose(f);
}
// ============================================================================
// 266. Double-Free Caused by Reference Count Wrap-Around (16-bit/8-bit Refcount
// Overflow)
// ============================================================================
struct Bug266SmallRef {
  uint8_t refs;
  void *buf;
};
void bug266_refcount_overflow_uaf(void) {
  struct Bug266SmallRef obj = {255, malloc(32)};
  obj.refs++; // ERROR: 8-bit uint8_t wraps from 255 to 0!
  if (obj.refs == 0) {
    free(obj.buf); // ERROR: Prematurely frees buffer while 256 active
                   // references still exist!
  }
}
// ============================================================================
// 267. Allocating Zero-Initialized Struct With Pointer Members Using Non-Zero
// memset Pattern
// ============================================================================
struct Bug267Ptrs {
  int *a;
  int *b;
};
void bug267_memset_poison_null_check(void) {
  struct Bug267Ptrs *p = (struct Bug267Ptrs *)malloc(sizeof(struct Bug267Ptrs));
  memset(p, 0xAA, sizeof(*p)); // Poisoning memory with 0xAA
  if (p->a != NULL) {          // Passes because 0xAAAAAAAAAAAAAAAA != NULL!
    free(p->a); // ERROR: Attempts to free poison address 0xAAAAAAAAAAAAAAAA ->
                // immediate crash
  }
  free(p);
}
// ============================================================================
// 268. Off-By-One Null Termination in readlink() (readlink Never Appends '\0')
// ============================================================================
void bug268_readlink_no_null(void) {
  char path;
  // readlink() NEVER appends a null terminator!
  ssize_t len = readlink("/proc/self/exe", path, sizeof(path));
  if (len >= 0) {
    // ERROR: If len == sizeof(path), path[len] writes out of bounds; if
    // omitted, printf over-reads!
    path[len] =
        '\0'; // ERROR: Stack buffer overflow when link length >= sizeof(path)
  }
}
// ============================================================================
// 269. Using strndup() Result Without NULL Check When Length Is Huge
// ============================================================================
void bug269_strndup_unchecked_null(const char *src) {
  char *copy = strndup(src, (size_t)-1); // If allocation fails, returns NULL
  copy = 'X'; // ERROR: Null pointer dereference if strndup fails
  free(copy);
}
// ============================================================================
// 270. Variadic va_list Reused Without va_copy() (Corrupts Stack/Register
// State)
// ============================================================================
#include <stdarg.h>
void bug270_vprintf_reuse_va_list(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap); // Consumes 'ap' (modifies internal pointers/offsets)
  vprintf(fmt, ap); // ERROR: Reusing consumed va_list without va_copy() ->
                    // reads garbage pointers/segfault!
  va_end(ap);
}
// ============================================================================
// 271. Missing va_end() Before Early Return in Variadic Function
// ============================================================================
void bug271_missing_va_end(int abort_early, ...) {
  va_list ap;
  va_start(ap, abort_early);
  if (abort_early) {
    return; // ERROR: C standard requires matching va_end(ap) in same function
            // before return -> UB
  }
  va_end(ap);
}
// ============================================================================
// 272. Pointer Subtraction Between char* and struct* Cast Without Char
// Alignment
// ============================================================================
struct Bug272Node {
  int x;
  int y;
};
void bug272_unaligned_struct_stride(void) {
  char raw;
  struct Bug272Node *p1 = (struct Bug272Node *)(raw + 1);
  struct Bug272Node *p2 =
      (struct Bug272Node *)(raw + 10); // Distance is 9 bytes (not a multiple of
                                       // sizeof(Bug272Node)=8)!
  ptrdiff_t elems = p2 - p1; // ERROR: Pointer subtraction when byte difference
                             // is not divisible by element size is UB
  (void)elems;
}
// ============================================================================
// 273. Freeing Array of Pointers Where One Slot Points to String Literal
// ============================================================================
void bug273_mixed_heap_and_literal_free(void) {
  char *words;
  words = strdup("heap_string_1");
  words = "static_literal_string"; // Points to .rodata, NOT heap!
  words = strdup("heap_string_2");
  for (int i = 0; i < 3; i++) {
    free(words[i]); // ERROR: i == 1 calls free() on read-only string literal ->
                    // crash!
  }
}
// ============================================================================
// 274. Passing Invalid Pointer to realloc() That Was Obtained From calloc() +
// Offset
// ============================================================================
void bug274_realloc_offset_pointer(void) {
  int *arr = (int *)calloc(4, sizeof(int));
  int *mid = arr + 2;
  int *resized =
      (int *)realloc(mid, 8 * sizeof(int)); // ERROR: 'mid' is not start of
                                            // allocation -> heap corruption
  free(resized ? resized : arr);
}
// ============================================================================
// 275. Custom RAII Scope Exit Macro Using setjmp/longjmp Skipping Inner Cleanup
// ============================================================================
static void bug275_cleanup_fn(void *p) { free((void **)p); }
#define BUG275_CLEANUP attribute((cleanup(bug275_cleanup_fn)))
jmp_buf bug275_outer_jmp;
void bug275_inner_scope(void) {
  BUG275_CLEANUP int p = (int *)malloc(sizeof(int));
  *p = 10;
  // longjmp bypasses normal stack unwinding in C, so attribute((cleanup)) NEVER
  // executes!
  longjmp(bug275_outer_jmp,
          1); // ERROR: Leaks 'p' despite RAII cleanup attribute!
}
void bug275_cleanup_bypassed_by_longjmp(void) {
  if (setjmp(bug275_outer_jmp) == 0) {
    bug275_inner_scope();
  }
}
// ============================================================================
// 276. GNU attribute((cleanup)) Registered With Wrong Pointer Indirection
// ============================================================================
static void bug276_bad_cleaner(int *p) {
  // ERROR: Compiler passes '&var' (which is int**) to cleanup function, NOT
  // 'var' (int*)!
  free(p); // Frees address of stack variable '&var' instead of heap pointer
           // 'var'!
}
void bug276_cleanup_wrong_indirection(void) {
  attribute((cleanup(bug276_bad_cleaner))) int *p = (int *)malloc(sizeof(int));
  *p = 5; // ERROR: On scope exit, calls free(&p) -> crashes freeing stack
          // address + leaks *p
}
// ============================================================================
// 277. Allocating Struct With Flexible Array Member on Heap Without Zeroing
// Header
// ============================================================================
struct Bug277Buffer {
  size_t cap;
  size_t len;
  char data[];
};
void bug277_uninit_fam_header(void) {
  struct Bug277Buffer *b =
      (struct Bug277Buffer *)malloc(sizeof(struct Bug277Buffer) + 32);
  // Forgot to initialize b->len = 0; b->cap = 32
  if (b->len < b->cap) { // ERROR: Reading uninitialized b->len and b->cap ->
                         // arbitrary OOB write
    b->data[b->len++] = 'A';
  }
  free(b);
}
// ============================================================================
// 278. Using Pointer to Destroyed Compound Literal Passed to Function
// ============================================================================
int *bug278_return_arg(int *p) { return p; }
void bug278_compound_literal_lifetime(void) {
  int *dangling = NULL;
  for (int i = 0; i < 1; i++) {
    dangling = bug278_return_arg((int[]){1, 2, 3});
  } // Compound literal lifetime ends at end of enclosing loop block
  printf(
      "%d\n",
      dangling); // ERROR: Use-after-scope dereferencing dead compound literal
}
// ============================================================================
// 279. Integer Division by Zero / Modulo Zero Before Buffer Allocation Index
// ============================================================================
void bug279_zero_mod_ring_index(size_t capacity) {
  // When capacity == 0, modulo 0 triggers SIGFPE / UB
  size_t idx = 5 % capacity; // ERROR: Division by zero when capacity == 0
  (void)idx;
}
// ============================================================================
// 280. Off-By-One in strncat When Remaining Space Equals Zero (unsigned wrap)
// ============================================================================
void bug280_strncat_remaining_underflow(void) {
  char buf = "123456789"; // strlen(buf) == 9, sizeof(buf) == 8 (already
                          // overflowed/unterminated!)
  // Suppose buf had 8 chars without '\0': sizeof(buf) - strlen(buf) - 1
  // underflows size_t!
  char full = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'}; // No '\0'
  size_t rem =
      sizeof(full) - strlen(full) -
      1; // ERROR: strlen reads past 'full', rem underflows to huge size_t!
  strncat(full, "Z", rem); // ERROR: Massive buffer overflow
}
// ============================================================================
// 281. Passing Overlap Between Restrict Pointers in snprintf(buf, sizeof(buf),
// "%s...", buf)
// ============================================================================
void bug281_snprintf_self_overlap(void) {
  char buf = "hello";
  // C standard forbids source and destination buffers overlapping in snprintf!
  snprintf(buf, sizeof(buf), "%s world",
           buf); // ERROR: Overlapping src & dst in snprintf is UB
                 // (truncates/corrupts)
}
// ============================================================================
// 282. Freeing Memory Inside Custom Comparator Passed to bsearch()
// ============================================================================
int *bug282_key_ptr = NULL;
int bug282_bsearch_cmp(const void *k, const void *elem) {
  free(bug282_key_ptr); // ERROR: Frees key mid-search; next iteration
                        // dereferences freed 'k'
  bug282_key_ptr = NULL;
  return ((const int)k - (const int)elem);
}
void bug282_bsearch_uaf(void) {
  int arr[] = {10, 20, 30, 40};
  bug282_key_ptr = (int *)malloc(sizeof(int));
  *bug282_key_ptr = 40;
  bsearch(bug282_key_ptr, arr, 4, sizeof(int), bug282_bsearch_cmp);
}
// ============================================================================
// 283. Misaligned SIMD / Vector Cast From malloc() With Over-Aligned Type
// (AVX-512 64B)
// ============================================================================
typedef float
    bug283_v16f attribute((vector_size(64))); // Requires 64-byte alignment
void bug283_overaligned_malloc_cast(void) {
  // Standard malloc() only guarantees 16-byte alignment (max_align_t), NOT
  // 64-byte alignment!
  bug283_v16f *vec = (bug283_v16f *)malloc(
      sizeof(bug283_v16f)); // ERROR: Misaligned for 64-byte AVX-512 load/store
  *vec = (bug283_v16f){
      0}; // ERROR: Segfault / GP fault on aligned vector move (vmovaps)
  free(vec);
}
// ============================================================================
// 284. Double-Free When Error Path Frees Caller-Owned Input Buffer
// ============================================================================
int bug284_callee_frees_caller_input(char *input) {
  if (!input || input == '\0') {
    free(input); // ERROR: Frees caller's buffer on error without documenting
                 // ownership transfer!
    return -1;
  }
  return 0;
}
void bug284_ownership_confusion_double_free(void) {
  char *data = strdup("");
  if (bug284_callee_frees_caller_input(data) < 0) {
    free(data); // ERROR: Caller also frees 'data' on error -> double free!
  }
}
// ============================================================================
// 285. Null-Terminated Array of Structs vs Counted Array Mismatch
// ============================================================================
struct Bug285KV {
  const char *k;
  const char *v;
};
void bug285_missing_sentinel_struct(void) {
  struct Bug285KV table[] = {
      {"host", "localhost"}, {"port", "8080"}
      // ERROR: Forgot {NULL, NULL} sentinel entry at end of table!
  };
  for (size_t i = 0; table[i].k != NULL;
       i++) { // ERROR: Reads stack past table until random NULL pointer
    printf("%s=%s\n", table[i].k, table[i].v);
  }
}
// ============================================================================
// 286. Casting Function Returning Pointer to Function Returning void
// (Discarding Allocation)
// ============================================================================
void *bug286_alloc_wrapper(size_t sz) { return malloc(sz); }
void bug286_discard_alloc_via_cast(void) {
  void(fn)(size_t) = (void()(size_t))bug286_alloc_wrapper;
  fn(128); // ERROR: UB calling mismatched function pointer + leaks 128 bytes
}
// ============================================================================
// 287. Using Pointer To Bit-Packed / Unaligned Struct Array Element With qsort
// ============================================================================
struct attribute((packed)) Bug287Item {
  char c;
  int val;
};
int bug287_cmp(const void *a, const void *b) {
  // ERROR: 'a' and 'b' point to packed structs (stride 5 bytes), so
  // &((Bug287Item*)a)->val is misaligned!
  return ((const struct Bug287Item *)a)->val -
         ((const struct Bug287Item *)b)->val;
}
void bug287_packed_qsort(void) {
  struct Bug287Item items = {{'a', 2}, {'b', 1}};
  qsort(items, 2, sizeof(struct Bug287Item), bug287_cmp);
}
// ============================================================================
// 288. Memory Leak in Thread Pool Queue Shutdown Leaving Pending Tasks Unfreed
// ============================================================================
struct Bug288Task {
  void(fn)(void);
  void *arg;
  struct Bug288Task *next;
};
void bug288_queue_shutdown_leak(void) {
  struct Bug288Task *head =
      (struct Bug288Task *)malloc(sizeof(struct Bug288Task));
  head->arg = malloc(256);
  head->next = NULL;
  // Shutdown drains queue nodes, forgetting to free task->arg payloads:
  while (head) {
    struct Bug288Task *tmp = head;
    head = head->next;
    free(tmp); // ERROR: Leaks tmp->arg (256 bytes) for every unexecuted task!
  }
}
// ============================================================================
// 289. Dangling Pointer in Observer/Listener Array When Removing During
// Iteration
// ============================================================================
void bug289_remove_during_dispatch(void) {
  int **listeners = (int **)malloc(3 * sizeof(int *));
  for (int i = 0; i < 3; i++) {
    listeners[i] = (int *)malloc(sizeof(int));
    *listeners[i] = i;
  }
  size_t count = 3;
  for (size_t i = 0; i < count; i++) {
    if (i == 0) {
      // Listener 0 unregisters and frees Listener 1 mid-loop:
      free(listeners);
      listeners = listeners;
      count--;
    }
    // On i == 1, accesses listeners (which was shifted into listeners, skipping
    // original listeners!) If listener 0 freed itself without shifting,
    // listeners[i] is a use-after-free!
    *listeners[i] += 10;
  }
  for (size_t i = 0; i < count; i++)
    free(listeners[i]);
  free(listeners);
}
// ============================================================================
// 290. Incorrect Size Calculation in Multi-Dimensional Dynamic Array
// Single-Block Allocation
// ============================================================================
void bug290_2d_single_block_bad_stride(void) {
  size_t rows = 4, cols = 5;
  // Allocates row pointers + data in one block, but uses sizeof(int) instead of
  // sizeof(int*) for header!
  int **mat = (int **)malloc(
      rows * sizeof(int) +
      rows * cols * sizeof(int));  // ERROR: Header too small on 64-bit!
  int *data = (int *)(mat + rows); // Overlaps into mat..mat!
  for (size_t r = 0; r < rows; r++) {
    mat[r] =
        data +
        r * cols; // ERROR: Writing mat and mat overwrites heap past allocation!
  }
  free(mat);
}
// ============================================================================
// 291. Null Pointer Passed to qsort / bsearch With Zero Elements (C99/C11 UB)
// ============================================================================
int bug291_dummy_cmp(const void *a, const void *b) {
  (void)a;
  (void)b;
  return 0;
}
void bug291_qsort_null_zero_count(void) {
  // Even with nmemb == 0, C standard requires 'base' to be a valid non-NULL
  // pointer!
  qsort(NULL, 0, sizeof(int),
        bug291_dummy_cmp); // ERROR: Undefined behavior; compiler can optimize
                           // out NULL checks
}
// ============================================================================
// 292. Using strtok() Concurrently Across Two Threads (Data Race + Corrupt
// Pointers)
// ============================================================================
void *bug292_strtok_thread(void *arg) {
  char *str = (char *)arg;
  // ERROR: strtok() uses a single static internal pointer shared across all
  // threads!
  char *tok = strtok(
      str,
      " "); // ERROR: Data race corrupting static save pointer -> OOB read/crash
  while (tok) {
    tok = strtok(NULL, " ");
  }
  return NULL;
}
// ============================================================================
// 293. Allocating Flexible Struct With Negative Count Converted to size_t
// ============================================================================
struct Bug293Vec {
  int count;
  int elems[];
};
struct Bug293Vec *bug293_new_vec(int count) {
  // If count == -1, (size_t)count * sizeof(int) wraps to SIZE_MAX - 3, or with
  // + sizeof(struct) wraps to 0!
  struct Bug293Vec *v = (struct Bug293Vec *)malloc(sizeof(struct Bug293Vec) +
                                                   (size_t)count * sizeof(int));
  if (v)
    v->count = count; // ERROR: When count == -1, malloc(0) succeeds and
                      // v->count writes out-of-bounds!
  return v;
}
// ============================================================================
// 294. Using Pointer After Converting to uintptr_t Across Different Process /
// Shared Memory Base
// ============================================================================
void bug294_shm_raw_pointer_storage(void) {
  int shmid = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
  if (shmid < 0)
    return;
  int **base1 = (int **)shmat(shmid, NULL, 0);
  int **base2 = (int **)shmat(
      shmid, NULL,
      0); // Mapped at a DIFFERENT virtual address in same/other process!
  if (base1 != (void *)-1 && base2 != (void *)-1) {
    base1 = (int)((char *)base1 +
                  64); // Stores raw virtual pointer inside shared memory
    shmdt(base1);      // Unmaps base1's virtual address range!
    **base2 = 42;      // ERROR: Dereferences base1's unmapped virtual address
                       // through base2 -> SIGSEGV!
    shmdt(base2);
  }
  shmctl(shmid, IPC_RMID, NULL);
}
// ============================================================================
// 295. Uninitialized Padding in Key Struct Passed to Hash Function (Lookup Miss
// / OOB)
// ============================================================================
struct Bug295Key {
  uint16_t tag;
  uint64_t id;
}; // 6 bytes of uninitialized padding between tag and id!
size_t bug295_hash_key(const struct Bug295Key *k) {
  const unsigned char *p = (const unsigned char *)k;
  size_t h = 0;
  for (size_t i = 0; i < sizeof(*k);
       i++) { // ERROR: Hashes 6 uninitialized padding bytes -> UB
    h = h * 131 + p[i];
  }
  return h;
}
// ============================================================================
// 296. Passing Stack Address to Kernel io_uring / epoll / Async Callback Across
// Functions
// ============================================================================
void bug296_register_async_req(void **out_user_data) {
  int local_req_id = 777;
  out_user_data = &local_req_id; // ERROR: Exposes stack address that dies
                                 // immediately upon return
}
void bug296_dangling_async_userdata(void) {
  void user_data = NULL;
  bug296_register_async_req(&user_data);
  printf("req=%d\n",
         (int)user_data); // ERROR: Dereferencing destroyed stack frame
}
// ============================================================================
// 297. Double-Free When Custom Destructor Recursively Frees Shared Child
// Subtree
// ============================================================================
struct Bug297Dag {
  int val;
  struct Bug297Dag left, right;
};
void bug297_free_dag(struct Bug297Dag n) {
  if (!n)
    return;
  bug297_free_dag(n->left);
  bug297_free_dag(n->right);
  free(n); // ERROR: If left and right point to the same shared child (DAG),
           // child is freed twice!
}
void bug297_dag_double_free(void) {
  struct Bug297Dag shared =
      (struct Bug297Dag *)calloc(1, sizeof(struct Bug297Dag));
  struct Bug297Dag *root =
      (struct Bug297Dag *)calloc(1, sizeof(struct Bug297Dag));
  root->left = shared;
  root->right = shared;  // Shared diamond node
  bug297_free_dag(root); // ERROR: Double-frees 'shared'
}
// ============================================================================
// 298. Off-By-One in Memory-Mapped Ring Buffer Mirroring Assumption
// ============================================================================
void bug298_unmirrored_ring_wrap_oob(void) {
  char *buf = (char *)malloc(16);
  size_t write_pos = 12;
  size_t msg_len = 8;
  // Assumes magic virtual-memory mirror exists past buf+16 when only 16 bytes
  // were malloc'd:
  memcpy(buf + write_pos, "12345678",
         msg_len); // ERROR: Writes 4 bytes past end of 16-byte heap buffer
  free(buf);
}
// ============================================================================
// 299. Using Pointer After Implicitly Truncating Return Type in Missing Header
// (64-bit)
// ============================================================================
void bug299_int_cast_roundtrip_ub(void) {
  char *heap_str = strdup("hello");
  // Storing pointer in 'long' on LLP64 (Windows 64-bit, where long is 32 bits!)
  // truncates upper 32 bits:
  long raw = (long)(intptr_t)
      heap_str; // ERROR: Truncates 64-bit pointer on 32-bit-long platforms
  char *restored = (char *)(intptr_t)raw;
  printf("%s\n", restored);
  free(heap_str);
}
// ============================================================================
// 300. Cleanup Macro Evaluating Expression Twice When Freeing Array Element
// (free(arr[i++]))
// ============================================================================
#define BUG300_FREE_AND_NULL(ptr)                                              \
  do {                                                                         \
    free(ptr);                                                                 \
    (ptr) = NULL;                                                              \
  } while (0)
void bug300_macro_side_effect_uaf(void) {
  int **list = (int **)malloc(2 * sizeof(int *));
  list = (int *)malloc(sizeof(int));
  list = (int *)malloc(sizeof(int));
  size_t i = 0;
  // Expands to: free(list[i++]); (list[i++]) = NULL; -> frees list, then nulls
  // list (leaking list)!
  BUG300_FREE_AND_NULL(list[i++]); // ERROR: Side-effect evaluated twice; leaks
                                   // list and corrupts index!
  free(list);
}
// ============================================================================
// 301. Allocating Array of Function Pointers With sizeof(void*) (Non-Portable /
// Harvard Arch UB)
// ============================================================================
typedef void(bug301_handler_t)(void);
void bug301_fn_ptr_sizeof_void_ptr(void) {
  // C standard does NOT guarantee sizeof(void()(void)) == sizeof(void*)!
  bug301_handler_t *handlers = (bug301_handler_t *)malloc(
      4 * sizeof(void *)); // ERROR: Should use sizeof(bug301_handler_t)
  handlers = NULL;
  free(handlers);
}
// ============================================================================
// 302. Passing Uninitialized Stack Buffer to realpath() / getcwd() With Size 0
// ============================================================================
void bug302_getcwd_zero_size(void) {
  char buf;
  // Passing non-NULL buf with size 0 fails with EINVAL and leaves 'buf'
  // completely uninitialized!
  char *res = getcwd(buf, 0);
  (void)res;
  printf("%s\n",
         buf); // ERROR: Reading uninitialized stack buffer when getcwd fails
}
// ============================================================================
// 303. Freeing Pointer Returned by dlerror() (Library-Owned Static/TLS String)
// ============================================================================
void bug303_free_dlerror(void) {
  dlopen("/nonexistent_lib_12345.so", RTLD_NOW);
  char *err = dlerror(); // Points to internal static/thread-local buffer, NOT
                         // malloc'd memory
  if (err) {
    free(err); // ERROR: Calling free() on dlerror() pointer corrupts heap
               // allocator
  }
}
// ============================================================================
// 304. Calling dlerror() Twice Without Copying String (Second Call Returns NULL
// -> Null Deref)
// ============================================================================
void bug304_dlerror_second_call_null(void) {
  dlopen("/nonexistent_lib_12345.so", RTLD_NOW);
  char *first = dlerror(); // Consumes and clears error state
  (void)first;
  char *second = dlerror(); // Returns NULL!
  printf(
      "Error length: %zu\n",
      strlen(second)); // ERROR: Passing NULL to strlen() -> immediate segfault
}
// ============================================================================
// Entry Point (Selectively uncomment any bug function to trigger/test)
// ============================================================================
int main(void) {
  bug205_getaddrinfo_leak();
  bug206_use_after_freeaddrinfo();
  bug207_glob_leak();
  bug208_globfree_on_failure();
  bug209_wordexp_leak();
  bug210_iconv_leak(1);
  bug211_iconv_null_check();
  bug212_sem_open_leak();
  bug213_copy_sem_t();
  bug214_mq_open_leak();
  bug215_popen_fclose_mismatch();
  bug216_fopen_pclose_mismatch();
  bug218_sockaddr_un_no_null();
  bug219_inet_ntoa_static_alias();
  bug221_double_init_leak();
  bug222_self_link_corruption();
  bug223_split_stale_tail_pointers();
  bug224_union_pointer_pun();
  bug225_stack_fam_access();
  bug226_underalloc_without_padding();
  bug227_cross_array_loop_bound();
  bug228_restrict_struct_alias();
  bug229_const_double_ptr_bypass();
  bug230_return_vla_ptr(4);
  bug232_relaxed_unlock_uaf();
  bug233_use_singleton();
  bug234_destroy_held_rwlock();
  bug236_vfork_heap_corruption();
  bug237_sigaltstack_uaf();
  bug238_sigaltstack_dangling_stack();
  bug239_posix_timer_uaf();
  bug241_scanset_overflow();
  bug242_strxfrm_no_null();
  bug243_stpncpy_missing_null();
  bug244_memccpy_unterminated();
  bug245_reallocarray_zero();
  bug246_header_size_underflow(4);
  bug248_ring_ptr_wrap_oob();
  bug249_make_slice();
  bug251_cleared_destructor_leak();
  bug254_nonvolatile_loop_counter_setjmp();
  bug257_unterminated_ptr_array();
  bug258_stale_end_pointer();
  bug259_canary_overwrite();
  bug260_free_setlocale_ptr();
  bug261_modify_setlocale_ptr();
  bug262_strerror_r_signature_confusion();
  bug264_mremap_dangling_alias();
  bug265_stdio_rw_switch_ub();
  bug266_refcount_overflow_uaf();
  bug267_memset_poison_null_check();
  bug268_readlink_no_null();
  bug269_strndup_unchecked_null("test");
  bug270_vprintf_reuse_va_list("%d\n", 42);
  bug271_missing_va_end(1, 42);
  bug272_unaligned_struct_stride();
  bug273_mixed_heap_and_literal_free();
  bug274_realloc_offset_pointer();
  bug275_cleanup_bypassed_by_longjmp();
  bug276_cleanup_wrong_indirection();
  bug277_uninit_fam_header();
  bug278_compound_literal_lifetime();
  bug279_zero_mod_ring_index(0);
  bug280_strncat_remaining_underflow();
  bug281_snprintf_self_overlap();
  bug282_bsearch_uaf();
  bug283_overaligned_malloc_cast();
  bug284_ownership_confusion_double_free();
  bug285_missing_sentinel_struct();
  bug286_discard_alloc_via_cast();
  bug287_packed_qsort();
  bug288_queue_shutdown_leak();
  bug289_remove_during_dispatch();
  bug290_2d_single_block_bad_stride();
  bug291_qsort_null_zero_count();
  bug293_new_vec(-1);
  bug294_shm_raw_pointer_storage();
  bug296_dangling_async_userdata();
  bug297_dag_double_free();
  bug298_unmirrored_ring_wrap_oob();
  bug299_int_cast_roundtrip_ub();
  bug300_macro_side_effect_uaf();
  bug301_fn_ptr_sizeof_void_ptr();
  bug302_getcwd_zero_size();
  bug303_free_dlerror();
  bug304_dlerror_second_call_null();
  return 0;
}
