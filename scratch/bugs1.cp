c
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <aio.h>
#include <alloca.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

    // ============================================================================
    // 1. Basic Memory Leak (Missing free)
    // ============================================================================
    void
    bug01_basic_leak(void) {
  int *p = (int *)malloc(sizeof(int));
  if (p)
    *p = 42;
  // ERROR: 'p' is never freed before returning
}

// ============================================================================
// 2. Null Pointer Dereference (Unchecked malloc)
// ============================================================================
void bug02_null_deref_malloc(void) {
  int *p = (int *)malloc((size_t)-1); // Fails and returns NULL
  *p = 10; // ERROR: Null pointer dereference (no NULL check)
  free(p);
}

// ============================================================================
// 3. Null Pointer Dereference via Struct Member
// ============================================================================
struct Bug03Node {
  int value;
};
void bug03_null_struct_member(void) {
  struct Bug03Node *n = NULL;
  n->value = 5; // ERROR: Dereferencing NULL pointer via arrow operator
}

// ============================================================================
// 4. Null Pointer Passed to libc Function (memcpy)
// ============================================================================
void bug04_null_to_memcpy(void) {
  char *src = NULL;
  char dst[10];
  memcpy(dst, src, 5); // ERROR: Passing NULL to memcpy is undefined behavior
}

// ============================================================================
// 5. Dangling Pointer: Use-After-Free (Read)
// ============================================================================
void bug05_uaf_read(void) {
  int *p = (int *)malloc(sizeof(int));
  *p = 99;
  free(p);            // 'p' is now a dangling pointer
  printf("%d\n", *p); // ERROR: Reading freed memory (Use-After-Free)
}

// ============================================================================
// 6. Dangling Pointer: Use-After-Free (Write)
// ============================================================================
void bug06_uaf_write(void) {
  int *p = (int *)malloc(sizeof(int));
  free(p);  // 'p' is now dangling
  *p = 123; // ERROR: Writing to freed memory corrupts heap metadata
}

// ============================================================================
// 7. Dangling Pointer: Returning Address of Local Stack Variable
// ============================================================================
int *bug07_helper(void) {
  int local = 42;
  return &local; // ERROR: 'local' is destroyed when function returns
}
void bug07_return_local_addr(void) {
  int *p = bug07_helper();
  printf("%d\n", *p); // ERROR: Dereferencing dangling stack pointer
}

// ============================================================================
// 8. Dangling Pointer: Block-Scoped Variable Outliving Scope
// ============================================================================
void bug08_block_scope_dangling(void) {
  int *p = NULL;
  {
    int temp = 77;
    p = &temp;
  }                   // 'temp' lifetime ends here
  printf("%d\n", *p); // ERROR: 'p' is dangling after closing brace
}

// ============================================================================
// 9. Dangling Pointer: Compound Literal Outliving Scope
// ============================================================================
void bug09_compound_literal_dangling(void) {
  int *p = NULL;
  if (1) {
    p = (int[]){10, 20,
                30}; // Automatic storage duration tied to enclosing block
  }
  printf("%d\n", p[0]); // ERROR: Compound literal destroyed at closing brace
}

// ============================================================================
// 10. Dangling Pointer After realloc() Moves Block
// ============================================================================
void bug10_realloc_dangling_old_ptr(void) {
  int *p1 = (int *)malloc(2 * sizeof(int));
  p1[0] = 10;
  int *p2 =
      (int *)realloc(p1, 10000 * sizeof(int)); // May move block and free p1
  if (p2) {
    p1[0] = 20; // ERROR: 'p1' is dangling if realloc moved the allocation
    free(p2);
  }
}

// ============================================================================
// 11. Dangling Pointer via Aliased Pointers (Two Pointers, One Allocation)
// ============================================================================
void bug11_aliased_dangling(void) {
  int *owner = (int *)malloc(sizeof(int));
  int *observer = owner;
  free(owner);    // Frees memory, sets neither to NULL
  *observer = 50; // ERROR: 'observer' is now a dangling pointer
}

// ============================================================================
// 12. Double Free
// ============================================================================
void bug12_double_free(void) {
  int *p = (int *)malloc(sizeof(int));
  free(p);
  free(p); // ERROR: Freeing already-freed pointer corrupts allocator
}

// ============================================================================
// 13. C-Style RAII Failure: Early Return Leaks Memory
// ============================================================================
int bug13_early_return_leak(int error_flag) {
  int *buf = (int *)malloc(100 * sizeof(int));
  if (!buf)
    return -1;
  if (error_flag) {
    return -1; // ERROR: No RAII in C; early return leaks 'buf'
  }
  free(buf);
  return 0;
}

// ============================================================================
// 14. C-Style RAII Failure: Partial Initialization Leak
// ============================================================================
struct Bug14Conn {
  int *rx_buf;
  int *tx_buf;
};
int bug14_partial_init_leak(struct Bug14Conn *c) {
  c->rx_buf = (int *)malloc(1024);
  if (!c->rx_buf)
    return -1;
  c->tx_buf = (int *)malloc((size_t)-1); // Fails
  if (!c->tx_buf) {
    return -1; // ERROR: Failed to roll back and free(c->rx_buf) on partial init
               // failure
  }
  return 0;
}

// ============================================================================
// 15. C-Style RAII Failure: File Handle Leak on Early Return
// ============================================================================
int bug15_file_leak(int abort_write) {
  FILE *f = fopen("app.log", "w");
  if (!f)
    return -1;
  if (abort_write) {
    return -1; // ERROR: File descriptor and FILE struct leaked without
               // fclose(f)
  }
  fclose(f);
  return 0;
}

// ============================================================================
// 16. C-Style RAII Failure: Mutex Deadlock on Early Return
// ============================================================================
pthread_mutex_t bug16_lock = PTHREAD_MUTEX_INITIALIZER;
int bug16_mutex_deadlock(int fail) {
  pthread_mutex_lock(&bug16_lock);
  if (fail) {
    return -1; // ERROR: No RAII lock guard in C; mutex stays locked forever
               // (deadlock)
  }
  pthread_mutex_unlock(&bug16_lock);
  return 0;
}

// ============================================================================
// 17. C-Style RAII Failure: longjmp Skipping Resource Cleanup
// ============================================================================
jmp_buf bug17_env;
void bug17_deep(void) {
  int *temp = (int *)malloc(256);
  longjmp(bug17_env,
          1); // ERROR: Unwinds stack directly to setjmp, skipping free(temp)
  free(temp);
}
void bug17_longjmp_leak(void) {
  if (setjmp(bug17_env) == 0) {
    bug17_deep();
  }
}

// ============================================================================
// 18. C-Style RAII Failure: Missing Custom Destructor for Nested Struct
// ============================================================================
struct Bug18String {
  char *data;
};
struct Bug18User {
  struct Bug18String name;
};
void bug18_nested_struct_leak(void) {
  struct Bug18User *u = (struct Bug18User *)malloc(sizeof(struct Bug18User));
  u->name.data = (char *)malloc(32);
  free(u); // ERROR: Only frees outer struct; inner 'u->name.data' is leaked
}

// ============================================================================
// 19. Shallow Struct Copy Causing Double Free
// ============================================================================
struct Bug19Array {
  int *items;
};
void bug19_shallow_copy_double_free(void) {
  struct Bug19Array a;
  a.items = (int *)malloc(4 * sizeof(int));
  struct Bug19Array b = a; // Bitwise copy duplicates raw pointer
  free(a.items);
  free(b.items); // ERROR: Double free due to shared raw pointer
}

// ============================================================================
// 20. Wild / Uninitialized Pointer Dereference
// ============================================================================
void bug20_wild_pointer(void) {
  int *p;  // Contains indeterminate garbage address
  *p = 42; // ERROR: Dereferencing wild pointer
}

// ============================================================================
// 21. Uninitialized Pointer Inside Struct
// ============================================================================
struct Bug21Holder {
  int *ptr;
};
void bug21_uninit_struct_ptr(void) {
  struct Bug21Holder *h =
      (struct Bug21Holder *)malloc(sizeof(struct Bug21Holder));
  if (h->ptr != NULL) { // ERROR: Reading uninitialized pointer value is UB
    *h->ptr = 10;       // ERROR: Dereferencing garbage pointer
  }
  free(h);
}

// ============================================================================
// 22. Stack Buffer Overflow (Out-of-Bounds Write)
Use code with caution.
    // ============================================================================
    void
    bug22_stack_overflow(void) {
  int arr[5];
  arr[5] = 99; // ERROR: Valid indices are 0..4; overwrites stack frame
}
// ============================================================================
// 23. Heap Buffer Overflow (Out-of-Bounds Write)
// ============================================================================
void bug23_heap_overflow(void) {
  int *arr = (int *)malloc(5 * sizeof(int));
  arr[5] = 99; // ERROR: Writes past end of allocated heap chunk
  free(arr);
}
// ============================================================================
// 24. Stack Buffer Underflow (Negative Index)
// ============================================================================
void bug24_stack_underflow(void) {
  int arr[5] = {1, 2, 3, 4, 5};
  arr[-1] = 0; // ERROR: Writes before start of array on stack
}
// ============================================================================
// 25. Heap Buffer Underflow (Corrupting Allocator Header)
// ============================================================================
void bug25_heap_underflow(void) {
  int *arr = (int *)malloc(5 * sizeof(int));
  arr[-1] =
      0; // ERROR: Overwrites heap chunk metadata immediately preceding 'arr'
  free(arr);
}
// ============================================================================
// 26. Off-By-One Loop Buffer Overflow
// ============================================================================
void bug26_off_by_one(void) {
  int *buf = (int *)malloc(10 * sizeof(int));
  for (int i = 0; i <= 10;
       i++) { // ERROR: '<= 10' writes 11 elements into 10-element buffer
    buf[i] = i;
  }
  free(buf);
}
// ============================================================================
// 27. Freeing Stack Memory
// ============================================================================
void bug27_free_stack(void) {
  int x = 10;
  int *p = &x;
  free(p); // ERROR: Passing non-heap stack address to free()
}
// ============================================================================
// 28. Freeing Read-Only String Literal
// ============================================================================
void bug28_free_literal(void) {
  char *s = "constant string";
  free(s); // ERROR: String literal is in read-only static storage, not heap
}
// ============================================================================
// 29. Modifying Read-Only String Literal
// ============================================================================
void bug29_modify_literal(void) {
  char *s = "hello";
  s[0] = 'H'; // ERROR: Writing to .rodata segment causes segmentation fault
}
// ============================================================================
// 30. Freeing Offset / Incremented Pointer
// ============================================================================
void bug30_free_offset_ptr(void) {
  int *p = (int *)malloc(5 * sizeof(int));
  p++;     // Pointer no longer points to start of heap block
  free(p); // ERROR: Invalid pointer passed to free()
}
// ============================================================================
// 31. Pointer Overwrite Memory Leak
// ============================================================================
void bug31_pointer_overwrite_leak(void) {
  int *p = (int *)malloc(sizeof(int));
  p = (int *)malloc(
      sizeof(int)); // ERROR: Original allocation pointer lost forever
  free(p);
}
// ============================================================================
// 32. realloc() Failure Overwriting Only Pointer (Leak)
// ============================================================================
void bug32_realloc_null_leak(void) {
  void *p = malloc(128);
  p = realloc(p, (size_t)-1); // ERROR: Returns NULL on failure, overwriting 'p'
                              // and leaking 128 bytes
  free(p);
}
// ============================================================================
// 33. Uninitialized Heap Read (malloc vs calloc)
// ============================================================================
void bug33_uninit_heap_read(void) {
  int *p = (int *)malloc(sizeof(int));
  printf("%d\n", *p); // ERROR: malloc does not initialize memory; reading
                      // indeterminate value
  free(p);
}
// ============================================================================
// 34. Uninitialized Stack Variable Read
// ============================================================================
void bug34_uninit_stack_read(void) {
  int x;
  int y = x + 1; // ERROR: Reading uninitialized automatic variable is undefined
                 // behavior
  printf("%d\n", y);
}
// ============================================================================
// 35. sizeof(pointer) Instead of sizeof(array)
// ============================================================================
void bug35_helper(int *buf) {
  memset(buf, 0,
         sizeof(buf)); // ERROR: Clears only 4 or 8 bytes (size of pointer)
}
void bug35_sizeof_pointer(void) {
  int data[10];
  bug35_helper(data);
}
// ============================================================================
// 36. Forgetting sizeof(type) in malloc
// ============================================================================
void bug36_missing_sizeof(void) {
  int *arr = (int *)malloc(
      10);     // ERROR: Allocates 10 bytes instead of 10 * sizeof(int)
  arr[5] = 42; // ERROR: Heap buffer overflow
  free(arr);
}
// ============================================================================
// 37. Missing +1 Byte for String Null Terminator
// ============================================================================
void bug37_missing_null_terminator(void) {
  const char *src = "hello";
  char *dst =
      (char *)malloc(strlen(src)); // ERROR: Allocates 5 bytes, needs 6 for '\0'
  strcpy(dst, src);                // ERROR: Writes '\0' 1 byte out of bounds
  free(dst);
}
// ============================================================================
// 38. Non-Null-Terminated Char Array Passed to String Function
// ============================================================================
void bug38_non_terminated_strlen(void) {
  char buf[5];
  memcpy(buf, "hello", 5);  // Missing '\0'
  size_t len = strlen(buf); // ERROR: Over-reads stack memory looking for '\0'
  printf("%zu\n", len);
}
// ============================================================================
// 39. Unbounded strcpy() Buffer Overflow
// ============================================================================
void bug39_unbounded_strcpy(void) {
  char small[4];
  strcpy(small, "overflowing_string"); // ERROR: Unbounded copy smashes stack
}
// ============================================================================
// 40. Overlapping Source and Destination in memcpy()
// ============================================================================
void bug40_overlap_memcpy(void) {
  char data[] = "12345678";
  memcpy(data + 2, data,
         5); // ERROR: Overlapping regions in memcpy is UB; must use memmove
}
// ============================================================================
// 41. Integer Overflow in Allocation Size
// ============================================================================
void bug41_int_overflow_malloc(void) {
  size_t n = (size_t)-1 / 2 + 2;
  int *p = (int *)malloc(
      n * sizeof(int)); // ERROR: Integer wraparound results in tiny allocation
  if (p) {
    p[10] = 1; // ERROR: Heap overflow on undersized buffer
    free(p);
  }
}
// ============================================================================
// 42. Signed-to-Unsigned Conversion Bypassing Bounds Check
// ============================================================================
void bug42_helper(char *dst, const char *src, int len) {
  if (len < 16) { // ERROR: Negative 'len' passes signed comparison, becomes
                  // huge size_t in memcpy
    memcpy(dst, src, (size_t)len); // ERROR: Massive buffer overflow / segfault
  }
}
void bug42_signed_unsigned_bypass(void) {
  char dst[16];
  char src[16] = {0};
  bug42_helper(dst, src, -5);
}
// ============================================================================
// 43. Pointer Arithmetic Past One-Past-The-End
// ============================================================================
void bug43_ptr_arith_oob(void) {
  int arr[4] = {1, 2, 3, 4};
  int *p = arr +
           6; // ERROR: Forming pointer more than 1 element past array end is UB
  (void)p;
}
// ============================================================================
// 44. Dereferencing One-Past-The-End Pointer
// ============================================================================
void bug44_deref_end_ptr(void) {
  int arr[4] = {1, 2, 3, 4};
  int *end = arr + 4; // Valid to point here
  int x = *end;       // ERROR: Dereferencing one-past-the-end pointer is UB
  (void)x;
}
// ============================================================================
// 45. Strict Aliasing Violation
// ============================================================================
void bug45_strict_aliasing(void) {
  float f = 1.0f;
  int *p = (int *)&f; // ERROR: Accessing float via incompatible int* breaks
                      // strict aliasing rules
  *p = 0;             // ERROR: Undefined behavior under compiler optimizations
  printf("%f\n", f);
}
// ============================================================================
// 46. Unaligned Pointer Dereference
// ============================================================================
void bug46_unaligned_access(void) {
  char *raw = (char *)malloc(16);
  int *unaligned =
      (int *)(raw + 1); // ERROR: Address is not aligned to sizeof(int)
  *unaligned =
      42; // ERROR: Undefined behavior / SIGBUS on strict alignment CPUs
  free(raw);
}
// ============================================================================
// 47. Calling fclose(NULL)
// ============================================================================
void bug47_fclose_null(void) {
  FILE *f = fopen("missing_file_xyz.txt", "r");
  fclose(
      f); // ERROR: 'f' is NULL; passing NULL to fclose() is undefined behavior
}
// ============================================================================
// 48. Double fclose() and Use-After-Close on FILE*
// ============================================================================
void bug48_double_fclose(void) {
  FILE *f = fopen("temp.txt", "w");
  if (f) {
    fclose(f);
    fprintf(f, "data"); // ERROR: Use-after-free of internal FILE structure
    fclose(f);          // ERROR: Double close of FILE*
  }
}
// ============================================================================
// 49. Freeing Outer 2D Array Before Inner Rows (Use-After-Free)
// ============================================================================
void bug49_2d_free_order(void) {
  int **grid = (int **)malloc(3 * sizeof(int *));
  for (int i = 0; i < 3; i++)
    grid[i] = (int *)malloc(3 * sizeof(int));
  free(grid); // Frees top-level pointer array first
  for (int i = 0; i < 3; i++) {
    free(grid[i]); // ERROR: Dereferencing freed 'grid' to read grid[i]
  }
}
// ============================================================================
// 50. Flexible Array Member Allocated Without Payload Size
// ============================================================================
struct Bug50Msg {
  int length;
  char payload[];
};
void bug50_fam_missing_payload(void) {
  struct Bug50Msg *msg = (struct Bug50Msg *)malloc(
      sizeof(struct Bug50Msg)); // ERROR: Forgot + payload bytes
  msg->payload[0] = 'X';        // ERROR: Heap buffer overflow
  free(msg);
}
// ============================================================================
// 51. Zero-Byte malloc() Dereference
// ============================================================================
void bug51_zero_malloc_deref(void) {
  int *p =
      (int *)malloc(0); // May return NULL or non-dereferenceable unique pointer
  if (p != NULL) {
    *p = 10; // ERROR: Dereferencing pointer returned by malloc(0) is undefined
             // behavior
    free(p);
  }
}
// ============================================================================
// 52. setvbuf() / setbuf() Dangling Stack Buffer
// ============================================================================
void bug52_helper(FILE *f) {
  char local_buf[BUFSIZ];
  setbuf(f, local_buf); // ERROR: local_buf dies on return, leaving stream with
                        // dangling buffer
}
void bug52_setbuf_dangling(void) {
  bug52_helper(stdout);
  printf("Writing into destroyed stack memory\n");
}
// ============================================================================
// 53. Linked List Traversal Use-After-Free (Advancing Pointer After Free)
// ============================================================================
struct Bug53Node {
  int val;
  struct Bug53Node *next;
};
void bug53_linked_list_uaf(void) {
  struct Bug53Node *head = (struct Bug53Node *)malloc(sizeof(struct Bug53Node));
  head->next = (struct Bug53Node *)malloc(sizeof(struct Bug53Node));
  head->next->next = NULL;
  for (struct Bug53Node *curr = head; curr != NULL;
       curr = curr->next) { // ERROR: Reads curr->next AFTER free(curr)
    free(curr);
  }
}
// ============================================================================
// 54. Doubly Linked List Removal Leaving Dangling Back/Forward Pointers
// ============================================================================
struct Bug54DNode {
  int val;
  struct Bug54DNode prev, next;
};
void bug54_remove(struct Bug54DNode target) {
  if (target->prev)
    target->prev->next = target->next;
  // ERROR: Forgot target->next->prev = target->prev; next node still points to
  // freed target
  free(target);
}
void bug54_dlist_dangling(void) {
  struct Bug54DNode a =
      (struct Bug54DNode *)calloc(1, sizeof(struct Bug54DNode));
  struct Bug54DNode *b =
      (struct Bug54DNode *)calloc(1, sizeof(struct Bug54DNode));
  a->next = b;
  b->prev = a;
  bug54_remove(a);
  if (b->prev)
    b->prev->val = 10; // ERROR: Use-after-free via dangling b->prev
  free(b);
}
// ============================================================================
// 55. Realloc Invalidating Interior Pointers Into Dynamic Buffer
// ============================================================================
void bug55_realloc_interior_ptr(void) {
  char *buf = (char *)malloc(16);
  strcpy(buf, "header:payload");
  char *payload_ptr = strchr(buf, ':') + 1; // Interior pointer into heap block
  char *new_buf = (char *)realloc(buf, 4096); // Moves block and frees old 'buf'
  if (new_buf) {
    buf = new_buf;
    payload_ptr[0] =
        'X'; // ERROR: 'payload_ptr' still points to old freed block
    free(buf);
  }
}
// ============================================================================
// 56. Thread Detached With Pointer to Caller Stack Frame
// ============================================================================
void *bug56_worker(void *arg) {
  int *shared_val = (int *)arg;
  usleep(10000);
  *shared_val = 42; // ERROR: Caller's stack frame already destroyed
  return NULL;
}
void bug56_spawn(void) {
  int local_status = 0;
  pthread_t tid;
  pthread_create(&tid, NULL, bug56_worker, &local_status);
  pthread_detach(
      tid); // ERROR: Function returns while thread still holds &local_status
}
// ============================================================================
// 57. Thread Cleanup Leak on pthread_cancel (Missing pthread_cleanup_push)
// ============================================================================
void *bug57_task(void *arg) {
  (void)arg;
  void *scratch = malloc(1024); // Allocated before cancellation point
  sleep(5); // Cancellation point: terminates here without running free(scratch)
  free(scratch); // ERROR: Unreachable on cancellation; memory leaked
  return NULL;
}
void bug57_pthread_cancel_leak(void) {
  pthread_t tid;
  pthread_create(&tid, NULL, bug57_task, NULL);
  usleep(1000);
  pthread_cancel(tid);
  pthread_join(tid, NULL);
}
// ============================================================================
// 58. Double-Checked Locking Without Atomics/Barriers
// ============================================================================
struct Bug58Cfg {
  int ready;
  int port;
};
struct Bug58Cfg *bug58_global = NULL;
pthread_mutex_t bug58_mtx = PTHREAD_MUTEX_INITIALIZER;
struct Bug58Cfg *bug58_get_cfg(void) {
  if (bug58_global == NULL) { // ERROR: Data race; another thread may observe
                              // non-NULL before fields are initialized
    pthread_mutex_lock(&bug58_mtx);
    if (bug58_global == NULL) {
      struct Bug58Cfg *tmp = (struct Bug58Cfg *)malloc(sizeof(struct Bug58Cfg));
      tmp->port = 8080;
      tmp->ready = 1;
      bug58_global =
          tmp; // Compiler/CPU may reorder store before initialization
    }
    pthread_mutex_unlock(&bug58_mtx);
  }
  return bug58_global;
}
// ============================================================================
// 59. TOCTOU Race Condition Between NULL Check and Free/Use Across Threads
// ============================================================================
int *bug59_shared = NULL;
void *bug59_thread_a(void *arg) {
  (void)arg;
  if (bug59_shared != NULL) { // Passes check, then thread B frees and nulls it
    bug59_shared = 10; // ERROR: Null pointer dereference OR use-after-free
  }
  return NULL;
}
void bug59_thread_b(void *arg) {
  (void)arg;
  free(bug59_shared);
  bug59_shared = NULL;
  return NULL;
}
// ============================================================================
// 60. Signal Handler Calling Non-Async-Signal-Safe malloc/free
// ============================================================================
void *bug60_buf = NULL;
void bug60_sig_handler(int sig) {
  (void)sig;
  free(bug60_buf); // ERROR: free() is not async-signal-safe; deadlocks or
                   // corrupts heap
  bug60_buf = malloc(64);
}
// ============================================================================
// 61. Returning Pointer Into Static Internal Buffer Overwritten by Second Call
// ============================================================================
const char *bug61_format_id(int id) {
  static char buf[32]; // Shared across all calls
  snprintf(buf, sizeof(buf), "ID-%d", id);
  return buf;
}
void bug61_static_buffer_alias(void) {
  const char *a = bug61_format_id(1);
  const char *b =
      bug61_format_id(2); // ERROR: Overwrites buffer pointed to by 'a'
  printf("%s vs %s\n", a, b);
}
// ============================================================================
// 62. Pointer Subtraction Across Two Distinct Allocations
// ============================================================================
void bug62_ptr_sub_distinct(void) {
  int *a = (int *)malloc(10 * sizeof(int));
  int *b = (int *)malloc(10 * sizeof(int));
  ptrdiff_t diff =
      b - a; // ERROR: Subtracting pointers to different allocated objects is UB
  (void)diff;
  free(a);
  free(b);
}
// ============================================================================
// 63. Relational Pointer Comparison Across Unrelated Objects
// ============================================================================
void bug63_ptr_cmp_distinct(void) {
  int *a = (int *)malloc(sizeof(int));
  int *b = (int *)malloc(sizeof(int));
  if (a < b) { // ERROR: Relational operators (<, <=, >, >=) on unrelated
               // pointers are UB
    *a = 1;
  }
  free(a);
  free(b);
}
// ============================================================================
// 64. Pointer Provenance Violation (Reconstructing Pointer via Integer Math)
// ============================================================================
void bug64_provenance_violation(void) {
  int *p = (int *)malloc(sizeof(int));
  int *q = (int *)malloc(sizeof(int));
  q = 10;
  uintptr_t addr_p = (uintptr_t)p;
  uintptr_t addr_q = (uintptr_t)q;
  int forged =
      (int *)(addr_p +
              (addr_q - addr_p)); // Forges address of q using p's provenance
  *forged =
      20; // ERROR: Provenance mismatch; compiler alias analysis may cause UB
  free(p);
  free(q);
}
// ============================================================================
// 65. Struct Padding Leak (Uninitialized Stack Bytes Written Externally)
// ============================================================================
struct Bug65Packet {
  char type;
  int payload;
};
void bug65_padding_info_leak(void) {
  struct Bug65Packet pkt;
  pkt.type = 1;
  pkt.payload = 42;
  // ERROR: 3 padding bytes between 'type' and 'payload' contain uninitialized
  // stack memory
  FILE *f = fopen("out.bin", "wb");
  if (f) {
    fwrite(&pkt, sizeof(pkt), 1, f); // Leaks uninitialized stack bytes
    fclose(f);
  }
}
// ============================================================================
// 66. memcmp() on Struct With Padding Bytes
// ============================================================================
struct Bug66Header {
  char flag;
  int count;
};
int bug66_memcmp_padding(void) {
  struct Bug66Header h1, h2;
  h1.flag = 1;
  h1.count = 10;
  h2.flag = 1;
  h2.count = 10;
  return memcmp(&h1, &h2,
                sizeof(struct Bug66Header)); // ERROR: Compares uninitialized
                                             // padding bytes; UB
}
// ============================================================================
// 67. Use-After-Scope of Variable Declared Inside switch Case
// ============================================================================
void bug67_switch_scope_dangling(int mode) {
  int *p = NULL;
  switch (mode) {
  case 1: {
    int local_in_case = 55;
    p = &local_in_case;
    break;
  } // 'local_in_case' lifetime ends here
  default:
    break;
  }
  printf("%d\n",
         *p); // ERROR: Dereferencing pointer to dead switch-block variable
}
// ============================================================================
// 68. Jumping Over Variable Initialization With goto
// ============================================================================
void bug68_goto_skips_init(void) {
  goto skip_init;
  int *ptr = NULL; // Initialization skipped!
skip_init:
  if (ptr != NULL) { // ERROR: 'ptr' has indeterminate value because goto
                     // bypassed initializer
    *ptr = 5;
  }
}
// ============================================================================
// 69. alloca() Inside a Loop Causing Progressive Stack Exhaustion
// ============================================================================
void bug69_alloca_in_loop(void) {
  for (int i = 0; i < 100000; i++) {
    // ERROR: alloca() allocates on stack and does NOT free until function
    // returns
    char *tmp = (char *)alloca(1024);
    tmp[0] = 'A';
  }
}
// ============================================================================
// 70. Deep Unbounded Recursion (Stack Overflow)
// ============================================================================
int bug70_infinite_recursion(int n) {
  char frame[1024];
  frame[0] = (char)n;
  if (n == 0)
    return frame[0];
  return bug70_infinite_recursion(
      n + 1); // ERROR: Infinite recursion exhausts stack memory
}
// ============================================================================
// 71. Variadic Function (va_arg) Type Mismatch Reading Wrong Memory Size
// ============================================================================
void bug71_va_arg_mismatch(int count, ...) {
  va_list args;
  va_start(args, count);
  for (int i = 0; i < count; i++) {
    double d = va_arg(
        args,
        double); // ERROR: Caller passed int (4B), va_arg reads double (8B) OOB
    printf("%f\n", d);
  }
  va_end(args);
}
// ============================================================================
// 72. Missing Sentinel NULL in Variadic Function Call
// ============================================================================
void bug72_free_all(void *first, ...) {
  va_list args;
  va_start(args, first);
  void *p = first;
  while (p != NULL) {
    free(p);
    p = va_arg(args, void *); // ERROR: Reads stack/registers past argument list
                              // if caller omits NULL
  }
  va_end(args);
}
// ============================================================================
// 73. Custom Arena Allocator Alignment Bug
// ============================================================================
struct Bug73Arena {
  char *base;
  size_t offset;
};
void *bug73_arena_alloc(struct Bug73Arena *a, size_t size) {
  void *ptr = a->base + a->offset; // ERROR: No alignment padding; subsequent
                                   // 64-bit allocations are misaligned
  a->offset += size;
  return ptr;
}
// ============================================================================
// 74. Reference Counting Underflow / Premature Destruction
// ============================================================================
struct Bug74RefObj {
  int refcount;
  int *payload;
};
void bug74_release(struct Bug74RefObj *obj) {
  obj->refcount--;
  if (obj->refcount <= 0) {
    free(obj->payload);
    free(obj);
  }
}
void bug74_refcount_underflow(void) {
  struct Bug74RefObj *o =
      (struct Bug74RefObj *)malloc(sizeof(struct Bug74RefObj));
  o->refcount = 1;
  o->payload = (int *)malloc(sizeof(int));
  bug74_release(o); // Frees 'o'
  bug74_release(o); // ERROR: Use-after-free & double-free on second release
}
// ============================================================================
// 75. Reference Counting Cyclic Graph Leak
// ============================================================================
struct Bug75Node {
  int refs;
  struct Bug75Node *peer;
};
void bug75_unref(struct Bug75Node *n) {
  if (--n->refs == 0) {
    if (n->peer)
      bug75_unref(n->peer);
    free(n);
  }
}
void bug75_cyclic_leak(void) {
  struct Bug75Node *a = (struct Bug75Node *)malloc(sizeof(struct Bug75Node));
  struct Bug75Node *b = (struct Bug75Node *)malloc(sizeof(struct Bug75Node));
  a->refs = 2;
  b->refs = 2;
  a->peer = b;
  b->peer = a;
  bug75_unref(a); // Drops to 1
  bug75_unref(
      b); // Drops to 1 -> ERROR: Neither reaches 0; permanent cyclic leak
}
// ============================================================================
// 76. mmap() Return Value Checked Against NULL Instead of MAP_FAILED
// ============================================================================
void bug76_mmap_null_check(void) {
  void *addr =
      mmap(NULL, 0, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (addr != NULL) { // ERROR: MAP_FAILED ((void*)-1) != NULL; passes check and
                      // dereferences -1!
    (int)addr = 1;    // ERROR: Segfault dereferencing (void*)-1
    munmap(addr, 0);
  }
}
// ============================================================================
// 77. munmap() With Wrong Length Leaving Partial Dangling Mapping / Leak
// ============================================================================
void bug77_munmap_partial_leak(void) {
  void *addr = mmap(NULL, 8192, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (addr != MAP_FAILED) {
    munmap(addr, 4096); // ERROR: Only unmaps first page; second 4096-byte page
                        // remains leaked
  }
}
// ============================================================================
// 78. Accessing Memory-Mapped File Beyond Truncated File Size (SIGBUS)
// ============================================================================
void bug78_mmap_sigbus(void) {
  int fd = open("tiny.bin", O_RDWR | O_CREAT | O_TRUNC, 0600);
  if (fd >= 0) {
    if (ftruncate(fd, 10) == 0) { // File is only 10 bytes
      char *map =
          (char *)mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
      if (map != MAP_FAILED) {
        map[5000] = 'Z'; // ERROR: Accessing second page beyond underlying file
                         // length triggers SIGBUS
        munmap(map, 8192);
      }
    }
    close(fd);
  }
}
// ============================================================================
// 79. dlclose() While Function Pointer Into Shared Library Still Exists
// ============================================================================
typedef void(bug79_fn)(void);
void bug79_dlclose_dangling_fn(void) {
  void handle = dlopen("./libplugin.so", RTLD_LAZY);
  if (handle) {
    bug79_fn fn = (bug79_fn)dlsym(handle, "run_plugin");
    dlclose(handle); // Unmaps shared library code pages from address space
    if (fn)
      fn(); // ERROR: Calling unloaded code pointer causes immediate segfault
  }
}
// ============================================================================
// 80. strtok() Modifying Read-Only String Literal
// ============================================================================
void bug80_strtok_literal(void) {
  char *literal = "a,b,c";
  char *tok = strtok(
      literal, ","); // ERROR: strtok writes '\0' into read-only string literal
  (void)tok;
}
// ============================================================================
// 81. snprintf() Return Value Used as Bytes Written Without Truncation Check
// ============================================================================
void bug81_snprintf_truncation_oob(void) {
  char buf[10];
  int needed = snprintf(buf, sizeof(buf),
                        "LongStringExceedingBuffer"); // Returns 25, NOT 9!
  buf[needed] = '\0'; // ERROR: Indexes buf[25], writing far past end of 10-byte
                      // stack buffer
}
// ============================================================================
// 82. fread() Buffer Not Null-Terminated Before Treating as C-String
// ============================================================================
void bug82_fread_missing_null(void) {
  char buf[16];
  FILE *f = fopen("data.txt", "rb");
  if (f) {
    size_t n =
        fread(buf, 1, sizeof(buf), f); // Reads up to 16 bytes with NO '\0'
    fclose(f);
    if (strstr(buf, "key")) { // ERROR: Over-reads stack buffer if file >= 16
                              // bytes without '\0'
      printf("found\n");
    }
    (void)n;
  }
}
// ============================================================================
// 83. qsort() Comparator Wrong Pointer Indirection Level
// ============================================================================
int bug83_cmp(const void *a, const void *b) {
  // ERROR: Elements of const char*[] are const char**, so a and b are const
  // char**, NOT const char*!
  return strcmp((const char *)a,
                (const char *)b); // Treats pointer bytes as string characters
}
void bug83_qsort_indirection(void) {
  const char *arr[] = {"banana", "apple", "cherry"};
  qsort(arr, 3, sizeof(const char *), bug83_cmp);
}
// ============================================================================
// 84. Passing Wrongly Sized Pointer to Out-Parameter
// ============================================================================
void bug84_write64(uint64_t *out) {
  out = 0x1122334455667788ULL; // Writes 8 bytes
}
void bug84_out_param_overflow(void) {
  uint32_t small = 0;
  bug84_write64(
      (uint64_t)&small); // ERROR: Writes 8 bytes into a 4-byte stack variable
}
// ============================================================================
// 85. Restrict Pointer Aliasing Violation
// ============================================================================
void bug85_add(int *restrict dst, const int *restrict src, size_t n) {
  for (size_t i = 0; i < n; i++)
    dst[i] += src[i];
}
void bug85_restrict_violation(void) {
  int data[6] = {1, 2, 3, 4, 5, 6};
  bug85_add(data + 1, data, 4); // ERROR: Overlapping ranges passed to
                                // 'restrict' pointers violates contract
}
// ============================================================================
// 86. Flexible Array Member Copied by Value (Truncates Dynamic Payload)
// ============================================================================
struct Bug86Blob {
  size_t len;
  char bytes[];
};
void bug86_fam_by_value(void) {
  struct Bug86Blob *b1 =
      (struct Bug86Blob *)malloc(sizeof(struct Bug86Blob) + 10);
  b1->len = 10;
  memset(b1->bytes, 'A', 10);
  struct Bug86Blob b2 = *b1; // ERROR: Struct assignment copies only 'len',
                             // dropping all 10 flexible bytes
  (void)b2;
  free(b1);
}
// ============================================================================
// 87. Container-Of Macro Applied to NULL Pointer
// ============================================================================
#define BUG87_CONTAINER_OF(ptr, type, member)                                  \
  ((type *)((char *)(ptr)-offsetof(type, member)))
struct Bug87Item {
  int id;
  int node;
};
void bug87_container_of_null(void) {
  int *member_ptr = NULL;
  struct Bug87Item *item = BUG87_CONTAINER_OF(
      member_ptr, struct Bug87Item, node); // ERROR: Wraps to 0xFFFFFFFFFFFFFFFC
  item->id = 1; // ERROR: Dereferencing invalid near-null wrapped address
}
// ============================================================================
// 88. Double-Pointer Ownership Confusion (Callee Frees, Caller Still Uses)
// ============================================================================
void bug88_consume(int **p) {
  free(p); // Frees caller's buffer, forgets p = NULL
}
void bug88_double_ptr_uaf(void) {
  int buf = (int)malloc(sizeof(int));
  *buf = 7;
  bug88_consume(&buf);
  if (buf != NULL) { // Passes because callee didn't null *p
    *buf = 8;        // ERROR: Use-after-free
  }
}
// ============================================================================
// 89. Out-Parameter Overwriting Existing Heap Allocation Without Freeing
// ============================================================================
void bug89_alloc(char **out) {
  out = (char)malloc(64); // Overwrites out without freeing previous allocation
}
void bug89_out_param_leak(void) {
  char p = NULL;
  bug89_alloc(&p);
  bug89_alloc(&p); // ERROR: First 64-byte allocation is leaked
  free(p);
}
// ============================================================================
// 90. Cache Storing Raw Pointer Without Deep Copy (Dangling Entry)
// ============================================================================
const char *bug90_cache = NULL;
void bug90_populate(void) {
  char temp_key[32];
  strcpy(temp_key, "session_123");
  bug90_cache = temp_key; // Stores raw pointer without strdup()
} // 'temp_key' dies here
void bug90_dangling_cache(void) {
  bug90_populate();
  printf(
      "%s\n",
      bug90_cache); // ERROR: Cache holds dangling pointer to dead stack frame
}
// ============================================================================
// 91. Callback Registration Missing Unregister on Object Destruction
// ============================================================================
typedef void(bug91_cb_t)(void ctx);
bug91_cb_t bug91_cb = NULL;
void *bug91_ctx = NULL;
void bug91_on_event(void *ctx) { (int)ctx += 1; }
void bug91_dangling_callback(void) {
  int *state = (int *)malloc(sizeof(int));
  *state = 0;
  bug91_cb = bug91_on_event;
  bug91_ctx = state;
  free(state); // ERROR: Destroyed context without unregistering callback
  if (bug91_cb)
    bug91_cb(bug91_ctx); // ERROR: Callback dereferences dangling bug91_ctx
}
// ============================================================================
// 92. Ring Buffer Wrap-Around Off-By-One Write
// ============================================================================
void bug92_ring_buffer_oob(void) {
  size_t cap = 8;
  int *ring = (int *)malloc(cap * sizeof(int));
  size_t head = 7;
  head = (head + 1) %
         (cap + 1); // ERROR: Modulo (cap + 1) allows head == 8 (out-of-bounds)
  ring[head] = 99;  // ERROR: Heap buffer overflow at index 8
  free(ring);
}
// ============================================================================
// 93. Tagged Pointer Dereferenced Without Untagging
// ============================================================================
void bug93_tagged_ptr_deref(void) {
  int *p = (int *)malloc(sizeof(int));
  int *tagged = (int *)((uintptr_t)p | 1ULL);
  tagged =
      42; // ERROR: Dereferencing tagged pointer without masking off low bit
  free((void)((uintptr_t)tagged & ~1ULL));
}
// ============================================================================
// 94. Freeing Tagged Pointer Without Clearing Tag Bits
// ============================================================================
void bug94_free_tagged_ptr(void) {
  int *p = (int *)malloc(sizeof(int));
  int *tagged = (int *)((uintptr_t)p | 1ULL);
  free(tagged); // ERROR: Allocator receives address + 1 (not start of chunk) ->
                // heap corruption
}
// ============================================================================
// 95. Multi-Level Tree Destruction Recursion Stack Overflow
// ============================================================================
struct Bug95Tree {
  struct Bug95Tree *left, right;
};
void bug95_destroy_tree(struct Bug95Tree root) {
  if (!root)
    return;
  bug95_destroy_tree(
      root->left); // ERROR: Unbounded recursion on skewed tree blows stack
  bug95_destroy_tree(root->right);
  free(root);
}
// ============================================================================
// 96. Partially Freed Array of Structs on Allocation Failure
// ============================================================================
struct Bug96Entry {
  char *name;
};
void bug96_partial_array_cleanup(void) {
  struct Bug96Entry *list = (struct Bug96Entry *)malloc(
      5 * sizeof(struct Bug96Entry)); // Uninitialized!
  for (int i = 0; i < 5; i++) {
    if (i == 3) {
      for (int j = 0; j < 5; j++) {
        free(list[j].name); // ERROR: list[3] and list[4] contain uninitialized
                            // garbage pointers!
      }
      free(list);
      return;
    }
    list[i].name = (char *)malloc(16);
  }
}
// ============================================================================
// 97. Custom Memory Pool Double-Return Corrupting Intrusive Free List
// ============================================================================
struct Bug97Slot {
  struct Bug97Slot *next;
};
struct Bug97Slot *bug97_freelist = NULL;
void bug97_pool_free(void *ptr) {
  struct Bug97Slot *slot = (struct Bug97Slot *)ptr;
  slot->next = bug97_freelist;
  bug97_freelist = slot;
}
void bug97_pool_double_free(void) {
  void *block = malloc(32);
  bug97_pool_free(block);
  bug97_pool_free(block); // ERROR: Double-return creates a cycle (block->next
                          // == block) in free list
  free(block);
}
// ============================================================================
// 98. Shifting Ownership via memmove of Struct Array Causing Double Free
// ============================================================================
struct Bug98Item {
  int *data;
};
void bug98_memmove_alias_double_free(void) {
  struct Bug98Item *arr =
      (struct Bug98Item *)malloc(3 * sizeof(struct Bug98Item));
  arr[0].data = (int *)malloc(sizeof(int));
  arr[1].data = (int *)malloc(sizeof(int));
  arr[2].data = (int *)malloc(sizeof(int));
  free(arr[0].data);
  memmove(&arr[0], &arr[1],
          2 * sizeof(struct Bug98Item)); // ERROR: arr[1].data and arr[2].data
                                         // now alias!
  for (int i = 0; i < 3; i++) {
    free(arr[i].data); // ERROR: Double free on index 1 and 2
  }
  free(arr);
}
// ============================================================================
// 99. Passing Stack Pointer to Asynchronous I/O (aio_read) Outliving Function
// ============================================================================
void bug99_async_io_stack_dangling(int fd) {
  char local_buf[64];
  struct aiocb cb;
  memset(&cb, 0, sizeof(cb));
  cb.aio_fildes = fd;
  cb.aio_buf = local_buf;
  cb.aio_nbytes = sizeof(local_buf);
  aio_read(&cb); // ERROR: Returns immediately; local_buf & cb die while async
                 // read is still in flight
}
// ============================================================================
// 100. Dangling Pointer via Variadic Format String (%s Mismatch)
// ============================================================================
void bug100_format_string_ptr(void) {
  int valid_num = 123;
  // ERROR: Passes integer 123 instead of char*; printf treats 0x0000007B as a
  // memory address
  printf("%s\n", valid_num);
}
// ============================================================================
// 101. Environment Variable Pointer Invalidated by Subsequent setenv()
// ============================================================================
void bug101_getenv_dangling(void) {
  setenv("MY_VAR", "initial_value", 1);
  char *val = getenv("MY_VAR"); // Points into internal environ array
  for (int i = 0; i < 100; i++) {
    setenv("OTHER_VAR", "growing_environment_forces_realloc",
           1); // Reallocates internal environ storage
  }
  printf(
      "%s\n",
      val); // ERROR: 'val' may be dangling after environment table reallocation
}
// ============================================================================
// 102. putenv() Given Automatic Stack Array (Dangling Environment Entry)
// ============================================================================
void bug102_helper(void) {
  char env_str[32];
  strcpy(env_str, "APP_MODE=prod");
  putenv(env_str); // ERROR: putenv() does NOT copy string; stores pointer
                   // directly to stack array
}
void bug102_putenv_stack_dangling(void) {
  bug102_helper();
  printf("%s\n", getenv("APP_MODE")); // ERROR: Reads destroyed stack frame
}
// ============================================================================
// 103. GNU attribute((cleanup)) RAII Macro Failing on Uninitialized Pointer
// ============================================================================
static void bug103_auto_free(void *p) { free((void **)p); }
#define BUG103_AUTO_FREE attribute((cleanup(bug103_auto_free)))
void bug103_cleanup_uninit(void) {
  BUG103_AUTO_FREE int
      ptr; // ERROR: Uninitialized pointer! Early return calls free(garbage)
  if (1)
    return;
  ptr = (int *)malloc(sizeof(int));
}
// ============================================================================
// 104. GNU attribute((cleanup)) Double-Free When Ownership Is Transferred
// ============================================================================
int *bug104_make_int(void) {
  BUG103_AUTO_FREE int *p = (int *)malloc(sizeof(int));
  p = 42;
  return p; // ERROR: Forgot p = NULL before returning; cleanup attribute frees
            // 'p' on return!
}
void bug104_cleanup_transfer_uaf(void) {
  int val = bug104_make_int();
  *val = 10; // ERROR: Use-after-free
}
// ============================================================================
// Entry Point (Selectively uncomment any bug function to trigger/test)
// ============================================================================
int main(void) {
  bug01_basic_leak();
  bug02_null_deref_malloc();
  bug03_null_struct_member();
  bug04_null_to_memcpy();
  bug05_uaf_read();
  bug06_uaf_write();
  bug07_return_local_addr();
  bug08_block_scope_dangling();
  bug09_compound_literal_dangling();
  bug10_realloc_dangling_old_ptr();
  bug11_aliased_dangling();
  bug12_double_free();
  bug13_early_return_leak(1);
  bug15_file_leak(1);
  bug16_mutex_deadlock(1);
  bug17_longjmp_leak();
  bug18_nested_struct_leak();
  bug19_shallow_copy_double_free();
  bug20_wild_pointer();
  bug21_uninit_struct_ptr();
  bug22_stack_overflow();
  bug23_heap_overflow();
  bug24_stack_underflow();
  bug25_heap_underflow();
  bug26_off_by_one();
  bug27_free_stack();
  bug28_free_literal();
  bug29_modify_literal();
  bug30_free_offset_ptr();
  bug31_pointer_overwrite_leak();
  bug32_realloc_null_leak();
  bug33_uninit_heap_read();
  bug34_uninit_stack_read();
  bug35_sizeof_pointer();
  bug36_missing_sizeof();
  bug37_missing_null_terminator();
  bug38_non_terminated_strlen();
  bug39_unbounded_strcpy();
  bug40_overlap_memcpy();
  bug41_int_overflow_malloc();
  bug42_signed_unsigned_bypass();
  bug43_ptr_arith_oob();
  bug44_deref_end_ptr();
  bug45_strict_aliasing();
  bug46_unaligned_access();
  bug47_fclose_null();
  bug48_double_fclose();
  bug49_2d_free_order();
  bug50_fam_missing_payload();
  bug51_zero_malloc_deref();
  bug52_setbuf_dangling();
  bug53_linked_list_uaf();
  bug54_dlist_dangling();
  bug55_realloc_interior_ptr();
  bug56_spawn();
  bug57_pthread_cancel_leak();
  bug61_static_buffer_alias();
  bug62_ptr_sub_distinct();
  bug63_ptr_cmp_distinct();
  bug64_provenance_violation();
  bug65_padding_info_leak();
  bug66_memcmp_padding();
  bug67_switch_scope_dangling(1);
  bug68_goto_skips_init();
  bug69_alloca_in_loop();
  bug70_infinite_recursion(1);
  bug71_va_arg_mismatch(1, 42);
  bug74_refcount_underflow();
  bug75_cyclic_leak();
  bug76_mmap_null_check();
  bug77_munmap_partial_leak();
  bug78_mmap_sigbus();
  bug79_dlclose_dangling_fn();
  bug80_strtok_literal();
  bug81_snprintf_truncation_oob();
  bug82_fread_missing_null();
  bug83_qsort_indirection();
  bug84_out_param_overflow();
  bug85_restrict_violation();
  bug86_fam_by_value();
  bug87_container_of_null();
  bug88_double_ptr_uaf();
  bug89_out_param_leak();
  bug90_dangling_cache();
  bug91_dangling_callback();
  bug92_ring_buffer_oob();
  bug93_tagged_ptr_deref();
  bug94_free_tagged_ptr();
  bug96_partial_array_cleanup();
  bug97_pool_double_free();
  bug98_memmove_alias_double_free();
  bug100_format_string_ptr();
  bug101_getenv_dangling();
  bug102_putenv_stack_dangling();
  bug103_cleanup_uninit();
  bug104_cleanup_transfer_uaf();
  return 0;
}
