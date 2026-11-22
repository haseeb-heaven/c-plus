c
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <regex.h>
#include <setjmp.h>
#include <signal.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

// ============================================================================
// 105. calloc() Integer Overflow Bypassing Zero-Initialization Assumptions
// ============================================================================
void bug105_calloc_assumption_mismatch(void) {
    // calloc checks overflow, but manual multiplication before malloc/calloc breaks
    size_t rows = 65536, cols = 65536;
    int* grid = (int*)calloc(rows * cols, sizeof(int)); // ERROR: rows * cols overflows to 0 before calloc sees it!
    if (grid) {
        grid = 1; // ERROR: Writing into 0-byte allocation (heap overflow)
        free(grid);
    }
}

// ============================================================================
// 106. aligned_alloc() Size Not a Multiple of Alignment (UB / Returns NULL)
// ============================================================================
void bug106_aligned_alloc_size_mismatch(void) {
    // C11 requires size to be an integral multiple of alignment
    void* p = aligned_alloc(64, 100); // ERROR: 100 is not a multiple of 64 -> undefined behavior / NULL
    *(char*)p = 'A'; // ERROR: Potential NULL dereference
    free(p);
}

// ============================================================================
// 107. posix_memalign() Uninitialized Output Pointer Used on Failure
// ============================================================================
void bug107_posix_memalign_uninit_out(void) {
    void* ptr; // Uninitialized!
    // Fails with EINVAL because alignment (3) is not a power of two; 'ptr' remains uninitialized
    if (posix_memalign(&ptr, 3, 64) != 0) {
        free(ptr); // ERROR: Passing uninitialized garbage pointer to free()!
    }
}

// ============================================================================
// 108. getcwd(NULL, 0) Heap Allocation Never Freed (POSIX Extension Leak)
// ============================================================================
void bug108_getcwd_malloc_leak(void) {
    char* cwd = getcwd(NULL, 0); // Allocates buffer via malloc() on POSIX/glibc
    if (cwd) {
        printf("%s\n", cwd);
        // ERROR: Forgot free(cwd); leaks heap buffer
    }
}

// ============================================================================
// 109. asprintf() Failure Leaving Output Pointer Indeterminate
// ============================================================================
void bug109_asprintf_error_free(void) {
    char* str = (char*)0xDEADBEEF;
    // On failure, glibc leaves 'str' undefined (not guaranteed NULL)
    if (asprintf(&str, "%s", "test") < 0) {
        free(str); // ERROR: Freeing indeterminate pointer if asprintf failed
    }
    free(str);
}

// ============================================================================
// 110. getline() Line Buffer Leak on EOF / Error Return
// ============================================================================
void bug110_getline_eof_leak(void) {
    FILE* f = fopen("/dev/null", "r");
    if (!f) return;
    char* line = NULL;
    size_t cap = 0;
    // getline() allocates 'line' via malloc() EVEN WHEN returning -1 on immediate EOF!
    if (getline(&line, &cap, f) == -1) {
        fclose(f);
        return; // ERROR: Forgot free(line); leaks buffer allocated prior to EOF
    }
    free(line);
    fclose(f);
}

// ============================================================================
// 111. open_memstream() Read Before fflush/fclose (Unflushed NULL/Stale Pointer)
// ============================================================================
void bug111_memstream_unflushed_read(void) {
    char* buf = NULL;
    size_t size = 0;
    FILE* ms = open_memstream(&buf, &size);
    fprintf(ms, "hello world");
    // ERROR: 'buf' and 'size' are not updated until fflush(ms) or fclose(ms)
    printf("%s\n", buf); // ERROR: Dereferencing NULL or stale reallocated pointer
    fclose(ms);
    free(buf);
}

// ============================================================================
// 112. open_memstream() Forgetting to Free Dynamic Buffer After fclose()
// ============================================================================
void bug112_memstream_buffer_leak(void) {
    char* buf = NULL;
    size_t size = 0;
    FILE* ms = open_memstream(&buf, &size);
    fprintf(ms, "data");
    fclose(ms); // Closes stream, but caller still owns 'buf'!
    // ERROR: Forgot free(buf); memory leak
}

// ============================================================================
// 113. fmemopen() With Too-Small Buffer Causing Missing Null Terminator
// ============================================================================
void bug113_fmemopen_no_null(void) {
    char backing;
    FILE* f = fmemopen(backing, sizeof(backing), "w");
    fwrite("1234", 1, 4, f); // Fills all 4 bytes; no room left for '\0'
    fclose(f);
    printf("%s\n", backing); // ERROR: Over-reads stack array because '\0' could not fit
}

// ============================================================================
// 114. realpath() With Fixed PATH_MAX Buffer Overflow on Long Paths
// ============================================================================
void bug114_realpath_fixed_buffer(void) {
    char resolved; // ERROR: Deliberately undersized buffer; realpath assumes PATH_MAX bytes
    if (realpath(".", resolved) != NULL) { // ERROR: Buffer overflow writing resolved path
        printf("%s\n", resolved);
    }
}

// ============================================================================
// 115. realpath(path, NULL) Heap Allocation Leaked
// ============================================================================
void bug115_realpath_malloc_leak(void) {
    char* resolved = realpath(".", NULL); // POSIX.1-2008 allocates result with malloc()
    if (resolved) {
        printf("%s\n", resolved);
        // ERROR: Forgot free(resolved)
    }
}

// ============================================================================
// 116. scandir() Namelist Elements and Array Partially Leaked
// ============================================================================
void bug116_scandir_partial_leak(void) {
    struct dirent** namelist = NULL;
    int n = scandir(".", &namelist, NULL, alphasort);
    if (n >= 0) {
        free(namelist); // ERROR: Only frees outer array; leaks every namelist[i] dirent struct!
    }
}

// ============================================================================
// 117. opendir() DIR* Handle Leak on Early Return (Missing closedir)
// ============================================================================
int bug117_opendir_leak(int abort_scan) {
    DIR* d = opendir(".");
    if (!d) return -1;
    if (abort_scan) {
        return -1; // ERROR: Leaks DIR* heap buffer and underlying file descriptor
    }
    closedir(d);
    return 0;
}

// ============================================================================
// 118. Use-After-closedir() on struct dirent* Pointer
// ============================================================================
void bug118_readdir_after_closedir(void) {
    DIR* d = opendir(".");
    if (!d) return;
    struct dirent* ent = readdir(d); // Points into DIR's internal dynamically allocated buffer
    closedir(d); // Frees DIR and its internal buffer
    if (ent) {
        printf("%s\n", ent->d_name); // ERROR: Use-after-free reading 'ent' after closedir()
    }
}

// ============================================================================
// 119. regcomp() Compiled Regex Leaked Without regfree()
// ============================================================================
void bug119_regcomp_leak(void) {
    regex_t re;
    if (regcomp(&re, "^[a-z]+$", REG_EXTENDED) == 0) {
        regexec(&re, "abc", 0, NULL, 0);
        // ERROR: Forgot regfree(&re); leaks internal NFA/DFA heap tables
    }
}

// ============================================================================
// 120. regfree() Called on Failed or Uninitialized regex_t
// ============================================================================
void bug120_regfree_on_failed_compile(void) {
    regex_t re;
    if (regcomp(&re, "[unclosed_bracket", REG_EXTENDED) != 0) {
        regfree(&re); // ERROR: regcomp failed; calling regfree on invalid regex_t is UB
    }
}

// ============================================================================
// 121. System V Shared Memory (shmat) Never Detached (shmdt Leak)
// ============================================================================
void bug121_shmat_detach_leak(void) {
    int shmid = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
    if (shmid >= 0) {
        void* addr = shmat(shmid, NULL, 0);
        shmctl(shmid, IPC_RMID, NULL);
        if (addr != (void*)-1) {
            *(int*)addr = 123;
            // ERROR: Forgot shmdt(addr); virtual address mapping leaks in process
        }
    }
}

// ============================================================================
// 122. Checking shmat() Return Against NULL Instead of (void*)-1
// ============================================================================
void bug122_shmat_null_check(void) {
    void* addr = shmat(-999, NULL, 0); // Fails and returns (void*)-1
    if (addr != NULL) { // ERROR: (void*)-1 != NULL; passes check!
        *(int*)addr = 1; // ERROR: Immediate segfault writing to 0xFFFFFFFFFFFFFFFF
    }
}

Use code with caution.
// ============================================================================
// 123. epoll_data_t Union Pointer Overwritten by fd Field (Dangling/Corrupt Ptr)
// ============================================================================
void bug123_epoll_union_overwrite(void) {
struct epoll_event ev;
ev.events = EPOLLIN;
ev.data.ptr = malloc(64); // Sets union bytes
ev.data.fd = 5;           // ERROR: Overwrites lower bytes of ev.data.ptr in the union!
free(ev.data.ptr);        // ERROR: Frees corrupted pointer (0x...00000005) -> crash + memory leak
}
// ============================================================================
// 124. epoll_ctl(EPOLL_CTL_DEL) Without Freeing Context Pointer in event.data.ptr
// ============================================================================
void bug124_epoll_del_context_leak(int epfd, int sockfd) {
struct epoll_event ev;
ev.events = EPOLLIN;
ev.data.ptr = malloc(128);
epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);
// Later on disconnect:
epoll_ctl(epfd, EPOLL_CTL_DEL, sockfd, NULL); // ERROR: Kernel removes fd, but ev.data.ptr is never freed!
}
// ============================================================================
// 125. Flexible Array Member Inside Union / Nested Struct Causing Overlap
// ============================================================================
struct Bug125Inner { int count; int data[]; };
struct Bug125Outer {
struct Bug125Inner hdr; // GCC extension allows FAM inside struct
int trailer;            // ERROR: 'trailer' occupies the exact same offset as hdr.data!
};
void bug125_nested_fam_overlap(void) {
struct Bug125Outer* o = (struct Bug125Outer*)malloc(sizeof(struct Bug125Outer) + 4 * sizeof(int));
o->trailer = 0x11111111;
o->hdr.data = 0x22222222; // ERROR: Silently overwrites o->trailer!
free(o);
}
// ============================================================================
// 126. Bit-Field Address Taken or Pointer Arithmetic Assumed Byte-Addressable
// ============================================================================
struct Bug126Flags {
unsigned int a : 4;
unsigned int b : 4;
unsigned int c : 24;
};
void bug126_bitfield_memset_partial(void) {
struct Bug126Flags f = {1, 2, 3};
// Assuming 'c' starts at byte offset 1 and writing 3 bytes via char*
char* p = (char*)&f + 1;
memset(p, 0xFF, 3); // ERROR: Bit-field layout/endianness is implementation-defined; corrupts adjacent bits
}
// ============================================================================
// 127. Unaligned Struct Pointer Cast From Network Byte Buffer
// ============================================================================
struct attribute((aligned(8))) Bug127Msg { uint64_t id; uint32_t len; };
void bug127_network_buffer_cast(void) {
char packet;
// Offset 3 is not 8-byte aligned
struct Bug127Msg* msg = (struct Bug127Msg*)(packet + 3); // ERROR: Misaligned pointer + strict aliasing UB
msg->id = 999; // ERROR: Unaligned 64-bit store
}
// ============================================================================
// 128. Packed Struct Member Address Passed to Function Expecting Aligned Pointer
// ============================================================================
struct attribute((packed)) Bug128Packed {
char tag;
int value; // Misaligned at offset 1!
};
void bug128_take_int(int* p) { p = 42; }
void bug128_packed_member_addr(void) {
struct Bug128Packed pk = {'A', 10};
bug128_take_int(&pk.value); // ERROR: Passing unaligned int causes UB / hardware alignment fault
}
// ============================================================================
// 129. Atomic Pointer ABA Problem in Lock-Free Stack Pop
// ============================================================================
struct Bug129Node { int val; struct Bug129Node* next; };
_Atomic(struct Bug129Node*) bug129_head = NULL;
struct Bug129Node* bug129_pop(void) {
struct Bug129Node* old_head = atomic_load(&bug129_head);
while (old_head != NULL) {
// ERROR: If another thread pops A, pops B, frees B, and pushes A back,
// old_head == A still matches, but old_head->next points to freed node B!
struct Bug129Node* next = old_head->next; // ERROR: Potential use-after-free on old_head->next
if (atomic_compare_exchange_weak(&bug129_head, &old_head, next)) {
return old_head;
}
}
return NULL;
}
// ============================================================================
// 130. Lock-Free Read-Copy-Update (RCU) Freeing Old Pointer While Readers Active
// ============================================================================
_Atomic(int*) bug130_config = NULL;
void bug130_reader(void) {
int* p = atomic_load(&bug130_config);
if (p) {
// Writer thread swaps and frees 'p' right here!
printf("%d\n", p); // ERROR: Use-after-free without grace period / hazard pointers
}
}
void bug130_writer(void) {
int next = (int*)malloc(sizeof(int));
next = 77;
int old = atomic_exchange(&bug130_config, next);
free(old); // ERROR: Frees 'old' immediately while concurrent readers may still hold it
}
// ============================================================================
// 131. pthread_key_create() Thread-Local Destructor Missing (TLS Leak)
// ============================================================================
pthread_key_t bug131_tls_key;
void* bug131_worker(void* arg) {
(void)arg;
void* tls_buf = malloc(512);
pthread_setspecific(bug131_tls_key, tls_buf);
return NULL; // ERROR: Key created with NULL destructor; tls_buf leaks when thread exits
}
void bug131_tls_leak(void) {
pthread_key_create(&bug131_tls_key, NULL); // ERROR: No destructor registered
pthread_t t;
pthread_create(&t, NULL, bug131_worker, NULL);
pthread_join(t, NULL);
pthread_key_delete(bug131_tls_key);
}
// ============================================================================
// 132. Passing Stack Address to pthread_setspecific() Read by Another Thread
// ============================================================================
void bug132_tls_stack_ptr(void) {
pthread_key_t key;
pthread_key_create(&key, free); // ERROR: Destructor will call free() on stack address!
int local = 10;
pthread_setspecific(key, &local); // ERROR: Stores stack pointer; thread exit invokes free(&local)
pthread_key_delete(key);
}
// ============================================================================
// 133. pthread_mutex_destroy() Called While Mutex Is Still Locked / Contended
// ============================================================================
void bug133_destroy_locked_mutex(void) {
pthread_mutex_t* mtx = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
pthread_mutex_init(mtx, NULL);
pthread_mutex_lock(mtx);
pthread_mutex_destroy(mtx); // ERROR: Destroying a locked mutex is undefined behavior in POSIX
free(mtx);
}
// ============================================================================
// 134. Moving/Copying pthread_mutex_t By Value (Invalidating Internal Pointers)
// ============================================================================
void bug134_copy_mutex_by_value(void) {
pthread_mutex_t m1;
pthread_mutex_init(&m1, NULL);
pthread_mutex_t m2 = m1; // ERROR: POSIX forbids copying mutexes; internal self-pointers break
pthread_mutex_lock(&m2); // ERROR: Undefined behavior operating on copied mutex
pthread_mutex_unlock(&m2);
pthread_mutex_destroy(&m1);
}
// ============================================================================
// 135. Copying FILE Struct By Value (Corrupting Internal Stream Buffers)
// ============================================================================
void bug135_copy_file_struct(void) {
FILE* f = fopen("test.txt", "w");
if (f) {
FILE copy = *f; // ERROR: Copying FILE struct duplicates internal buffer pointers & locks
fprintf(©, "bad"); // ERROR: Undefined behavior / double-free of internal FILE buffer
fclose(f);
}
}
// ============================================================================
// 136. Passing Non-Malloc Pointer to setvbuf() With _IOFBF and Exiting Scope
// ============================================================================
void bug136_setvbuf_stack_scope(void) {
FILE* f = tmpfile();
if (!f) return;
{
char scoped_buf;
setvbuf(f, scoped_buf, _IOFBF, sizeof(scoped_buf));
} // 'scoped_buf' dies here, before fclose(f) flushes!
fprintf(f, "flush into dead stack");
fclose(f); // ERROR: fclose flushes stream using dangling 'scoped_buf' pointer
}
// ============================================================================
// 137. Using freopen() And Still Writing to Old FILE* Variable
// ============================================================================
void bug137_freopen_old_pointer(void) {
FILE* f1 = fopen("a.txt", "w");
if (f1) {
FILE* f2 = freopen("/nonexistent/invalid/path", "w", f1); // Fails and closes f1!
(void)f2;
fprintf(f1, "write after failed freopen"); // ERROR: f1 is already closed by freopen even on failure!
}
}
// ============================================================================
// 138. tmpnam() Race Condition and Static Buffer Overwrite
// ============================================================================
void bug138_tmpnam_static_overwrite(void) {
char* p1 = tmpnam(NULL); // Returns pointer to internal static buffer
char* p2 = tmpnam(NULL); // ERROR: Overwrites internal buffer pointed to by p1
printf("%s %s\n", p1, p2);
}
// ============================================================================
// 139. strerror() Pointer Overwritten by Subsequent Library Call
// ============================================================================
void bug139_strerror_static_overwrite(void) {
char* err1 = strerror(ENOENT); // Points to static buffer (not thread-safe)
char* err2 = strerror(ENOMEM); // ERROR: Overwrites err1 buffer
printf("%s | %s\n", err1, err2);
}
// ============================================================================
// 140. localtime() / gmtime() Static tm Struct Overwritten Across Calls
// ============================================================================
void bug140_localtime_static_overwrite(void) {
time_t t1 = 0, t2 = 86400;
struct tm* tm1 = gmtime(&t1); // Returns pointer to static struct tm
struct tm* tm2 = gmtime(&t2); // ERROR: Overwrites *tm1 in place!
printf("day1=%d day2=%d\n", tm1->tm_mday, tm2->tm_mday); // Prints same day twice
}
// ============================================================================
// 141. Reentrant strtok_r() Saveptr Pointing Into Freed Heap String
// ============================================================================
void bug141_strtok_r_freed_buffer(void) {
char* str = strdup("one,two,three");
char* saveptr = NULL;
char* tok1 = strtok_r(str, ",", &saveptr);
(void)tok1;
free(str); // Frees underlying buffer
char* tok2 = strtok_r(NULL, ",", &saveptr); // ERROR: saveptr points inside freed 'str' (use-after-free)
(void)tok2;
}
// ============================================================================
// 142. strtol() Endptr Dereferenced When Input String Was NULL
// ============================================================================
void bug142_strtol_null_endptr(void) {
char* end = NULL;
long v = strtol(NULL, &end, 10); // ERROR: Passing NULL as first arg to strtol is UB
(void)v;
}
// ============================================================================
// 143. sprintf() Formatting Pointer/String Into Exact-Sized Buffer Without '\0'
// ============================================================================
void bug143_sprintf_off_by_one(void) {
char buf; // Holds max 4 chars + '\0'
sprintf(buf, "%05d", 12345); // ERROR: Writes 5 digits + '\0' = 6 bytes into 5-byte buffer
}
// ============================================================================
// 144. scanf("%s") Without Field Width Specifier (Unbounded Stack Overflow)
// ============================================================================
void bug144_scanf_unbounded(void) {
char word;
// Simulating unbounded string read
sscanf("supercalifragilisticexpialidocious", "%s", word); // ERROR: Overflows 8-byte stack buffer
}
// ============================================================================
// 145. scanf("%c") Expecting Null Terminator (Passing Single Char to printf %s)
// ============================================================================
void bug145_scanf_char_as_string(void) {
char ch;
sscanf("A", "%c", &ch); // Reads 1 char, does NOT write '\0'
printf("%s\n", &ch); // ERROR: Reads stack past 'ch' until a zero byte is encountered
}
// ============================================================================
// 146. Wide Character (wchar_t) Buffer Allocated With Byte Length Instead of sizeof(wchar_t)
// ============================================================================
#include <wchar.h>
void bug146_wchar_malloc_bytes(void) {
const wchar_t* src = L"Hello";
// ERROR: wcslen returns character count (5), NOT byte count ((5 + 1) * sizeof(wchar_t))!
wchar_t* dst = (wchar_t*)malloc(wcslen(src) + 1);
wcscpy(dst, src); // ERROR: Massive heap buffer overflow (writes 24 bytes into 6-byte buffer)
free(dst);
}
// ============================================================================
// 147. Mixing Byte Count and Element Count in wmemset / wmemcpy
// ============================================================================
void bug147_wmemcpy_byte_count(void) {
wchar_t src = L"ABCD";
wchar_t dst;
// ERROR: 3rd argument is number of wchar_t elements, NOT sizeof(dst) bytes!
wmemcpy(dst, src, sizeof(dst)); // ERROR: Copies 5 * 4 = 20 wchar_t elements (80 bytes) into 20-byte buffer!
}
// ============================================================================
// 148. Returning Pointer Into Static Array Inside Recursive Function
// ============================================================================
char* bug148_recursive_static(int depth) {
static char scratch; // Shared across ALL recursive stack frames!
if (depth <= 0) {
strcpy(scratch, "base");
return scratch;
}
char* child = bug148_recursive_static(depth - 1);
snprintf(scratch, sizeof(scratch), "d=%d:%s", depth, child); // ERROR: Overwrites 'child' while reading it (UB)
return scratch;
}
// ============================================================================
// 149. Modifying Variable Twice Between Sequence Points via Pointers (UB)
// ============================================================================
void bug149_unsequenced_pointer_write(void) {
int x = 1;
int* p = &x;
int* q = &x;
int y = (*p)++ + (*q)++; // ERROR: Unsequenced modification and access to same memory location -> UB
(void)y;
}
// ============================================================================
// 150. Dereferencing Pointer Before Null Check (Compiler Eliminates Null Check!)
// ============================================================================
void bug150_deref_before_null_check(int* p) {
int val = *p; // Dereferences 'p' unconditionally
if (p == NULL) { // ERROR: Compiler optimizes away this check assuming 'p' cannot be NULL after deref!
return;
}
(void)val;
}
// ============================================================================
// 151. Null Pointer Offset Arithmetic (NULL + offset Is Undefined Behavior)
// ============================================================================
void bug151_null_pointer_arithmetic(void) {
char* base = NULL;
char* offset_ptr = base + 16; // ERROR: Pointer arithmetic on NULL is undefined behavior in C
(void)offset_ptr;
}
// ============================================================================
// 152. Zero-Length Array Pointer Arithmetic Outside Struct Bounds
// ============================================================================
struct Bug152Header { int type; };
void bug152_past_struct_cast(void) {
struct Bug152Header* h = (struct Bug152Header*)malloc(sizeof(struct Bug152Header));
char* payload = (char*)(h + 1); // Points one-past-the-end of allocation
*payload = 'X'; // ERROR: Dereferencing one-past-the-end heap allocation (heap buffer overflow)
free(h);
}
// ============================================================================
// 153. Sub-Object Bounds Violation (Overrunning Array Member Into Next Struct Field)
// ============================================================================
struct Bug153Record {
char name;
int is_admin;
};
void bug153_subobject_overflow(void) {
struct Bug153Record rec = {"bob", 0};
// Even though within sizeof(struct Bug153Record), overrunning 'name' into 'is_admin' is UB
strcpy(rec.name, "alice"); // ERROR: Writes 6 bytes into 4-byte member 'name', corrupting 'is_admin'
}
// ============================================================================
// 154. Variable-Length Array (VLA) Used in sizeof After Pointer Decay
// ============================================================================
void bug154_vla_decay_sizeof(int n, int arr[n]) {
// Parameter 'int arr[n]' decays to 'int* arr'
memset(arr, 0, sizeof(arr)); // ERROR: sizeof(arr) is sizeof(int*), NOT n * sizeof(int)!
}
// ============================================================================
// 155. VLA Lifetime Ending on Backward goto Jump (Use-After-Scope)
// ============================================================================
void bug155_vla_backward_goto(void) {
int* saved = NULL;
int count = 0;
again:
if (count == 1) {
saved = 99; // ERROR: VLA from iteration 0 was destroyed when jumping backward past its declaration!
return;
}
int vla;
vla = 1;
saved = vla;
count++;
goto again; // Jumping backward over VLA declaration ends its lifetime
}
// ============================================================================
// 156. setjmp/longjmp Clobbering Non-Volatile Local Pointers (Double Free / Leak)
// ============================================================================
jmp_buf bug156_jmp;
void bug156_setjmp_clobber(void) {
char* p = NULL; // ERROR: Not declared 'volatile'; value after longjmp is indeterminate!
if (setjmp(bug156_jmp) == 0) {
p = (char*)malloc(32);
longjmp(bug156_jmp, 1);
} else {
free(p); // ERROR: 'p' may still be NULL (leak) or indeterminate (UB) due to register caching
}
}
// ============================================================================
// 157. Calling longjmp() After Function That Called setjmp() Has Returned
// ============================================================================
jmp_buf bug157_dangling_env;
void bug157_setup_jmp(void) {
setjmp(bug157_dangling_env); // Saves stack frame of bug157_setup_jmp
} // Stack frame destroyed here!
void bug157_longjmp_dead_frame(void) {
bug157_setup_jmp();
longjmp(bug157_dangling_env, 1); // ERROR: Restores stack pointer to destroyed stack frame (catastrophic UB)
}
// ============================================================================
// 158. Signal Handler Using siglongjmp Out of malloc() (Permanent Heap Deadlock)
// ============================================================================
sigjmp_buf bug158_senv;
void bug158_sig_jmp(int sig) {
(void)sig;
siglongjmp(bug158_senv, 1); // ERROR: If signal interrupted malloc(), heap lock remains held forever!
}
// ============================================================================
// 159. Custom Allocator Realloc Losing Alignment Guarantee
// ============================================================================
void bug159_realloc_breaks_alignment(void) {
void* p = aligned_alloc(64, 64); // 64-byte aligned
if (p) {
void* p2 = realloc(p, 128); // ERROR: Standard realloc() only guarantees max_align_t (8/16B), breaking 64B alignment!
free(p2 ? p2 : p);
}
}
// ============================================================================
// 160. Freeing Memory Allocated by Another Shared Library / Incompatible Heap
// ============================================================================
void bug160_mismatched_allocator_free(void) {
// Simulating custom allocator wrapper that prepends a metadata header
char* raw = (char*)malloc(64 + sizeof(size_t));
(size_t)raw = 64;
void* user_ptr = raw + sizeof(size_t);
free(user_ptr); // ERROR: Passing offset user pointer directly to system free() instead of custom_free()
}
// ============================================================================
// 161. Intrusive Linked List Node Freed While Still Linked in List
// ============================================================================
struct Bug161List { struct Bug161List* next; };
void bug161_free_while_linked(void) {
struct Bug161List head = {NULL};
struct Bug161List* item = (struct Bug161List*)malloc(sizeof(struct Bug161List));
item->next = NULL;
head.next = item;
free(item); // ERROR: Freed node without unlinking from 'head.next'
if (head.next) {
head.next->next = NULL; // ERROR: Use-after-free via list head
}
}
// ============================================================================
// 162. Sentinel Node In Doubly Linked List Freed During Full List Tear-Down
// ============================================================================
struct Bug162Node { struct Bug162Node prev, next; };
void bug162_free_stack_sentinel(void) {
struct Bug162Node sentinel; // Allocated on stack!
struct Bug162Node elem = (struct Bug162Node)malloc(sizeof(struct Bug162Node));
sentinel.next = elem; sentinel.prev = elem;
elem->next = &sentinel; elem->prev = &sentinel;
struct Bug162Node* curr = sentinel.next;
while (curr != NULL) { // ERROR: Loop doesn't stop at &sentinel; calls free(&sentinel)!
struct Bug162Node* nxt = curr->next;
free(curr); // ERROR: Frees stack-allocated 'sentinel' on second iteration
curr = nxt;
}
}
// ============================================================================
// 163. Hash Table Resize Re-Hashing Freed Keys
// ============================================================================
void bug163_rehash_freed_key(void) {
char* key = strdup("user_42");
const char* table_slot = key;
free(key); // External caller freed key without removing from table
// Later table resize hashes stored pointer:
unsigned long h = 0;
for (const char* s = table_slot; *s; s++) { // ERROR: Reading freed key string during rehash
h = h * 31 + (unsigned char)*s;
}
(void)h;
}
// ============================================================================
// 164. Binary Search Tree Rotation Leaving Dangling Parent Pointer
// ============================================================================
struct Bug164Node { int k; struct Bug164Node left, right, parent; };
void bug164_bst_dangling_parent(void) {
struct Bug164Node root = (struct Bug164Node)calloc(1, sizeof(struct Bug164Node));
struct Bug164Node child = (struct Bug164Node*)calloc(1, sizeof(struct Bug164Node));
root->left = child;
child->parent = root;
// Promote child to root and free old root, forgetting child->parent = NULL
free(root);
if (child->parent) {
child->parent->k = 5; // ERROR: Use-after-free via stale parent pointer
}
free(child);
}
// ============================================================================
// 165. Reference Count Increment Race Condition (Non-Atomic ++ on Shared Struct)
// ============================================================================
struct Bug165Shared { int refcount; char* data; };
void bug165_racy_retain(struct Bug165Shared* s) {
s->refcount++; // ERROR: Non-atomic increment across threads loses updates -> premature free (UAF)
}
// ============================================================================
// 166. Double-Free Caused by Error Handling Goto Label Falling Through
// ============================================================================
int bug166_goto_fallthrough_double_free(int step) {
int* a = (int*)malloc(16);
int* b = (int*)malloc(16);
if (step == 1) goto err_b;
if (step == 2) goto err_a;
free(b);
free(a);
return 0;
err_b:
free(b);
// ERROR: Forgot 'return -1;' or 'goto done;'; falls through to err_a and frees 'b' again if swapped!
free(b); // ERROR: Double free in cleanup block
err_a:
free(a);
return -1;
}
// ============================================================================
// 167. Cleanup Goto Label Freeing Uninitialized Local Pointers
// ============================================================================
int bug167_goto_uninit_free(int fail_early) {
int* a; // ERROR: Not initialized to NULL!
int* b; // ERROR: Not initialized to NULL!
if (fail_early) goto cleanup;
a = (int*)malloc(16);
b = (int*)malloc(16);
cleanup:
free(a); // ERROR: Calls free() on uninitialized garbage pointer!
free(b); // ERROR: Calls free() on uninitialized garbage pointer!
return -1;
}
// ============================================================================
// 168. Macro Argument Evaluated Twice Causing Double Allocation / Leak
// ============================================================================
#define BUG168_MAX(a, b) ((a) > (b) ? (a) : (b))
void bug168_macro_double_eval_leak(void) {
// malloc() is evaluated twice; one allocated pointer is never captured and leaks!
void* p = BUG168_MAX(malloc(32), malloc(64)); // ERROR: Allocates 3 times total, leaking 2 blocks!
free(p);
}
// ============================================================================
// 169. Macro Freeing Pointer Without Parentheses or Block Braces
// ============================================================================
#define BUG169_SAFE_FREE(p) free(p); p = NULL
void bug169_macro_no_do_while(int cond) {
int* ptr = (int*)malloc(sizeof(int));
if (cond)
BUG169_SAFE_FREE(ptr); // ERROR: Expands to 'if (cond) free(ptr); ptr = NULL;' -> nulls unconditionally!
}
// ============================================================================
// 170. Pointer Cast From Integer Truncating 64-Bit Address to 32-Bit int
// ============================================================================
void bug170_pointer_truncation_32bit(void) {
int* p = (int*)malloc(sizeof(int));
int truncated = (int)(uintptr_t)p; // ERROR: Truncates upper 32 bits of 64-bit pointer!
int* restored = (int*)(uintptr_t)(unsigned int)truncated;
*restored = 42; // ERROR: Segfault dereferencing truncated invalid pointer
free(p);
}
// ============================================================================
// 171. Signed Pointer Difference Overflow on Huge Array (> PTRDIFF_MAX)
// ============================================================================
void bug171_ptrdiff_overflow(void) {
// On systems where object size exceeds PTRDIFF_MAX, p2 - p1 overflows signed ptrdiff_t (UB)
char* buf = (char*)malloc(1024);
if (buf) {
char* start = buf + 500;
char* end = buf;
size_t len = (size_t)(end - start); // ERROR: Negative ptrdiff_t wraps to huge size_t (~18 exabytes)!
(void)len;
free(buf);
}
}
// ============================================================================
// 172. Negative Array Index Computed From Unsigned Underflow
// ============================================================================
void bug172_unsigned_index_underflow(void) {
int arr = {10, 20, 30, 40};
size_t idx = 0;
arr[idx - 1] = 99; // ERROR: idx - 1 wraps to SIZE_MAX (0xFFFFFFFFFFFFFFFF) -> massive OOB write!
}
// ============================================================================
// 173. Off-By-One in memmove When Shifting Array Elements Right
// ============================================================================
void bug173_memmove_shift_overflow(void) {
int arr = {1, 2, 3, 4, 5};
// Wants to shift indices 0..3 into 1..4, but passes 5 elements instead of 4
memmove(&arr, &arr, 5 * sizeof(int)); // ERROR: Writes 5 ints starting at arr (writes arr out-of-bounds)
}
// ============================================================================
// 174. Passing Non-Pointer Integer Zero Instead of NULL to Variadic Sentinel
// ============================================================================
void bug174_varargs_zero_instead_of_null(void) {
// On 64-bit LP64, literal '0' is a 32-bit int, whereas NULL / (void*)0 is 64-bit pointer!
// Upper 32 bits of the 64-bit argument register/stack slot remain garbage!
// execl("/bin/echo", "echo", "hi", 0); // ERROR: 32-bit 0 instead of (char*)NULL leaves upper 32 bits dirty
}
// ============================================================================
// 175. Returning Pointer to Thread-Local Storage (_Thread_local) to Another Thread
// ============================================================================
_Thread_local int bug175_tls_var = 42;
void* bug175_reader_thread(void* arg) {
int* other_thread_tls = (int*)arg;
usleep(10000);
other_thread_tls = 99; // ERROR: Owner thread exited and destroyed its TLS segment -> use-after-free!
return NULL;
}
void bug175_owner_thread(void* arg) {
pthread_t* reader = (pthread_t*)arg;
pthread_create(reader, NULL, bug175_reader_thread, &bug175_tls_var);
return NULL; // Exits immediately, unmapping bug175_tls_var
}
// ============================================================================
// 176. Stack Variable Address Passed to epoll/select/poll Loop Outliving Helper
// ============================================================================
struct pollfd* bug176_register_poll(int fd) {
struct pollfd pfd = { .fd = fd, .events = 1 };
return &pfd; // ERROR: Returns pointer to automatic stack struct
}
// ============================================================================
// 177. Modifying Buffer Passed to qsort/bsearch While Comparator Reads Global
// ============================================================================
int* bug177_base_ptr = NULL;
int bug177_cmp(const void* a, const void* b) {
free(bug177_base_ptr); // ERROR: Mutating/freeing array inside qsort comparator causes immediate UAF
bug177_base_ptr = NULL;
return ((const int)a - (const int)b);
}
void bug177_qsort_self_free(void) {
bug177_base_ptr = (int*)malloc(4 * sizeof(int));
bug177_base_ptr = 3; bug177_base_ptr = 1;
bug177_base_ptr = 4; bug177_base_ptr = 2;
qsort(bug177_base_ptr, 4, sizeof(int), bug177_cmp);
}
// ============================================================================
// 178. atexit() Handler Accessing Heap Memory Already Freed Before exit()
// ============================================================================
char* bug178_global_log = NULL;
void bug178_on_exit(void) {
if (bug178_global_log) {
printf("Exit log: %s\n", bug178_global_log); // ERROR: Use-after-free if freed before exit()
}
}
void bug178_atexit_uaf(void) {
bug178_global_log = strdup("shutting down");
atexit(bug178_on_exit);
free(bug178_global_log); // Freed without setting bug178_global_log = NULL
}
// ============================================================================
// 179. Dynamic Array Growth Formula Multiplying Capacity == 0 (0 * 2 == 0)
// ============================================================================
void bug179_zero_capacity_growth(void) {
size_t cap = 0;
int* arr = NULL;
// Grow capacity:
cap = cap * 2; // ERROR: 0 * 2 is still 0!
arr = (int*)realloc(arr, cap * sizeof(int)); // Allocates 0 bytes
arr = 42; // ERROR: Null pointer dereference / OOB write into 0-byte allocation
free(arr);
}
// ============================================================================
// 180. Using pointer-to-pointer (T**) Assuming Contiguous 2D Array Layout
// ============================================================================
void bug180_jagged_array_memset(void) {
int** rows = (int**)malloc(3 * sizeof(int*));
for (int i = 0; i < 3; i++) rows[i] = (int*)malloc(4 * sizeof(int));
// ERROR: rows is NOT contiguous with rows; memset overwrites heap metadata and unrelated chunks!
memset(rows, 0, 3 * 4 * sizeof(int));
for (int i = 0; i < 3; i++) free(rows[i]);
free(rows);
}
// ============================================================================
// 181. Casting Function Pointer With Incompatible Signature and Calling It
// ============================================================================
void bug181_target(int a, int b, int c, int d, int e, int f, int* out_ptr) {
*out_ptr = a + b + c + d + e + f; // 7th arg comes from stack, which caller never pushed!
}
void bug181_fn_ptr_mismatch(void) {
void (fn)(int) = (void ()(int))bug181_target;
fn(1); // ERROR: Calling function through incompatible pointer type is UB; reads stack garbage as pointer!
}
// ============================================================================
// 182. Unaligned / Over-Aligned Allocation Freed With Wrong Deallocator
// ============================================================================
void bug182_mmap_freed_with_free(void) {
void* page = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
if (page != MAP_FAILED) {
free(page); // ERROR: Passing mmap() region to free() instead of munmap() corrupts heap allocator!
}
}
// ============================================================================
// 183. Heap Allocation Passed to munmap() Instead of free()
// ============================================================================
void bug183_malloc_freed_with_munmap(void) {
void* p = malloc(4096);
if (p) {
munmap(p, 4096); // ERROR: Unmapping malloc's brk/arena page out from under glibc allocator!
}
}
// ============================================================================
// 184. String Concatenation Loop Without Tracking Remaining Capacity (Quadratic Overflow)
// ============================================================================
void bug184_strcat_loop_overflow(void) {
char dst = "";
const char* parts[] = {"alpha", "beta", "gamma", "delta"};
for (int i = 0; i < 4; i++) {
// ERROR: Passes total sizeof(dst) instead of remaining space (sizeof(dst) - strlen(dst) - 1)
strncat(dst, parts[i], sizeof(dst)); // ERROR: Overflows 16-byte buffer on 4th iteration
}
}
// ============================================================================
// 185. Using memchr() Result Without Checking NULL Before Pointer Subtraction
// ============================================================================
void bug185_memchr_null_sub(void) {
const char buf[] = "abcdef";
const char* found = (const char*)memchr(buf, 'Z', sizeof(buf)); // Returns NULL
ptrdiff_t idx = found - buf; // ERROR: Subtracting valid pointer from NULL pointer is UB!
(void)idx;
}
// ============================================================================
// 186. Const-Cast Writing Into Read-Only Global Const Object
// ============================================================================
const int bug186_ro_global = 100;
void bug186_write_const_global(void) {
int* mutable_ptr = (int*)&bug186_ro_global; // Discards const qualifier
*mutable_ptr = 200; // ERROR: Writing to const object in .rodata triggers SIGSEGV / UB
}
// ============================================================================
// 187. Compound Literal Inside Loop Reused Across Iterations (Aliasing Trap)
// ============================================================================
void bug187_compound_literal_loop_alias(void) {
int* ptrs;
for (int i = 0; i < 3; i++) {
// C99/C11 creates ONE single automatic object per enclosing block, NOT per iteration!
ptrs[i] = (int[]){ i * 10 }; // ERROR: All 3 pointers point to the exact same stack address!
}
printf("%d %d %d\n", *ptrs, *ptrs, *ptrs); // Prints "20 20 20" instead of "0 10 20"
}
// ============================================================================
// 188. File Descriptor Closed Twice (Closing Reused FD Belonging to Another Thread)
// ============================================================================
void bug188_double_close_fd(void) {
int fd = open("/dev/null", O_RDONLY);
if (fd >= 0) {
close(fd);
// Another thread opens a critical file and gets assigned the exact same lowest-available 'fd'!
close(fd); // ERROR: Double close accidentally closes the other thread's file descriptor!
}
}
// ============================================================================
// 189. fdopen() Failure Leaving Raw File Descriptor Leaked
// ============================================================================
void bug189_fdopen_fail_leak(void) {
int fd = open("/dev/null", O_RDONLY);
if (fd >= 0) {
// Fails because mode "w" conflicts with O_RDONLY
FILE* f = fdopen(fd, "w");
if (!f) {
// ERROR: fdopen() does NOT close 'fd' on failure; forgot close(fd)!
return;
}
fclose(f);
}
}
// ============================================================================
// 190. fclose() Called on FILE* AND close() Called on Underlying fileno(f)
// ============================================================================
void bug190_fclose_and_close_fd(void) {
FILE* f = fopen("/dev/null", "r");
if (f) {
int fd = fileno(f);
fclose(f); // Already closes 'fd'
close(fd); // ERROR: Double-closes underlying file descriptor!
}
}
// ============================================================================
// 191. Zero-Length memcpy(dst, NULL, 0) / memset(NULL, 0, 0) (C99/C11 UB)
// ============================================================================
void bug191_zero_len_null_memcpy(void) {
char* p = NULL;
// Even when n == 0, C standard specifies pointers passed to memcpy/memset MUST be valid (non-NULL)!
memcpy(p, p, 0); // ERROR: Undefined behavior; compiler may optimize out subsequent NULL checks!
}
// ============================================================================
// 192. Using errno After Cleanup Function (free/fclose) That Clobbers errno
// ============================================================================
int bug192_cleanup_clobbers_errno(void) {
void* buf = malloc(64);
FILE* f = fopen("/nonexistent/file", "r"); // Sets errno = ENOENT
if (!f) {
free(buf); // Cleanup before saving errno (or fclose/syslog that sets errno = 0)
return errno; // ERROR: Cleanup calls can overwrite errno before caller inspects it
}
free(buf);
fclose(f);
return 0;
}
// ============================================================================
// 193. Uninitialized Flexible Array Padding / Struct Copy With Trailing Bytes
// ============================================================================
struct Bug193Dyn { int n; char buf[]; };
void bug193_memcpy_wrong_struct_size(void) {
struct Bug193Dyn* src = (struct Bug193Dyn*)malloc(sizeof(struct Bug193Dyn) + 32);
struct Bug193Dyn* dst = (struct Bug193Dyn*)malloc(sizeof(struct Bug193Dyn) + 32);
src->n = 32;
memset(src->buf, 'Z', 32);
// ERROR: sizeof(*src) only copies header (4 bytes), leaving dst->buf completely uninitialized!
memcpy(dst, src, sizeof(*src));
printf("%c\n", dst->buf); // ERROR: Reading uninitialized heap memory
free(src);
free(dst);
}
// ============================================================================
// 194. Pointer Arithmetic on void* (Non-Standard GNU Extension vs C Standard UB)
// ============================================================================
void bug194_void_ptr_arithmetic(void) {
void* p = malloc(16);
if (p) {
// In strict ISO C, arithmetic on void* is invalid (constraint violation); in GNU C it steps by 1 byte
void* p2 = p + 4;
(void)p2;
free(p);
}
}
// ============================================================================
// 195. Self-Referential Struct Cloned With memcpy() Leaving Internal Pointer Pointing to Old Struct
// ============================================================================
struct Bug195SelfRef {
char inline_buf;
char* active_ptr; // Points either to 'inline_buf' (SSO) or heap
};
void bug195_sso_memcpy_dangling(void) {
struct Bug195SelfRef* s1 = (struct Bug195SelfRef*)malloc(sizeof(struct Bug195SelfRef));
strcpy(s1->inline_buf, "small");
s1->active_ptr = s1->inline_buf; // Points inside s1!
struct Bug195SelfRef* s2 = (struct Bug195SelfRef*)malloc(sizeof(struct Bug195SelfRef));
memcpy(s2, s1, sizeof(struct Bug195SelfRef)); // Copies s2->active_ptr = &s1->inline_buf!
free(s1); // Frees s1!
printf("%s\n", s2->active_ptr); // ERROR: s2->active_ptr still points into freed s1 (use-after-free)!
free(s2);
}
// ============================================================================
// 196. Doubly-Indirect Pointer Passed to Free Function Expecting Single Pointer
// ============================================================================
void bug196_wrong_indirection_free(void) {
int* p = (int*)malloc(sizeof(int));
int** pp = &p;
free(pp); // ERROR: Passes stack address '&p' instead of heap address 'p' to free()!
}
// ============================================================================
// 197. Casting Function Return Value (long) to Pointer Without Prototype (Implicit Int Truncation)
// ============================================================================
void bug197_unaligned_int_to_ptr(void) {
uintptr_t bad_addr = 0x1001; // Odd address
int* p = (int*)bad_addr;     // ERROR: Creating misaligned pointer for int (alignof(int) == 4) is UB in C
(void)p;
}
// ============================================================================
// 198. Memory Leak in Socket Accept Loop on Fork/Thread Creation Failure
// ============================================================================
void bug198_accept_ctx_leak(int client_fd) {
int* fd_arg = (int*)malloc(sizeof(int));
*fd_arg = client_fd;
pthread_t tid;
// Simulate thread creation failure (e.g., EAGAIN / resource limit)
if (pthread_create(&tid, NULL, NULL, fd_arg) != 0) {
close(client_fd);
// ERROR: Forgot free(fd_arg) when pthread_create fails!
}
}
// ============================================================================
// 199. Using Invalidated Pointer After Calling realloc(ptr, 0)
// ============================================================================
void bug199_realloc_zero_ub(void) {
void* p = malloc(32);
// In C17/C23, realloc(ptr, 0) has implementation-defined / UB behavior (may free p and return NULL)
void* p2 = realloc(p, 0); // ERROR: Calling realloc with size 0 is UB in C23 and non-portable
free(p2);
}
// ============================================================================
// 200. Bitwise Shift Out-of-Bounds Used to Compute Array Index
// ============================================================================
void bug200_shift_ub_array_index(void) {
int table = {0};
int shift = 32; // Shifting 32-bit int by >= 32 bits is UB!
int idx = (1 << shift) & 31; // ERROR: Undefined behavior produces unpredictable index -> OOB access
table[idx] = 1;
}
// ============================================================================
// 201. Destroying Condvar While Another Thread Is Still Blocked in pthread_cond_wait
// ============================================================================
void bug201_destroy_active_condvar(void) {
pthread_cond_t* cv = (pthread_cond_t*)malloc(sizeof(pthread_cond_t));
pthread_cond_init(cv, NULL);
// Assuming another thread is waiting on 'cv':
pthread_cond_destroy(cv); // ERROR: Destroying a condvar with active waiters is UB
free(cv); // ERROR: Waiting thread wakes up and accesses freed 'cv' (use-after-free)
}
// ============================================================================
// 202. Stack-Allocated Recursive Struct / Infinite Size Struct Pointer Confusion
// ============================================================================
struct Bug202Node {
int val;
struct Bug202Node* children;
size_t count;
};
void bug202_deep_clone_shallow_leak(struct Bug202Node* dst, const struct Bug202Node* src) {
*dst = src; // Copies src->children pointer directly
dst->children = (struct Bug202Node)malloc(src->count * sizeof(struct Bug202Node));
// ERROR: If dst already owned a previous dst->children allocation, it was overwritten and leaked!
}
// ============================================================================
// 203. Allocating sizeof(pointer) Instead of sizeof(pointer) on Nested Pointer
// ============================================================================
struct Bug203BigStruct { double matrix; };
void bug203_sizeof_ptr_instead_of_pointee(void) {
struct Bug203BigStruct* arr = (struct Bug203BigStruct**)malloc(10 * sizeof(struct Bug203BigStruct*));
// ERROR: Allocates sizeof(arr) (8 bytes) instead of sizeof(arr[0]) (128 bytes)!
arr = (struct Bug203BigStruct)malloc(sizeof(arr));
arr->matrix = 1.0; // ERROR: Massive heap buffer overflow (writes 128 bytes into 8-byte chunk)
free(arr);
free(arr);
}
// ============================================================================
// 204. GNU attribute((cleanup)) Applied to Array or Struct With Uninitialized Sub-Handles
// ============================================================================
struct Bug204Resource { int* a; int* b; };
static void bug204_cleanup_res(struct Bug204Resource* r) {
free(r->a);
free(r->b);
}
#define BUG204_AUTO_RES attribute((cleanup(bug204_cleanup_res)))
void bug204_partial_struct_cleanup(int fail) {
BUG204_AUTO_RES struct Bug204Resource res; // ERROR: Uninitialized struct members!
res.a = (int*)malloc(sizeof(int));
if (fail) {
return; // ERROR: Cleanup runs and calls free(res.b) on uninitialized garbage pointer!
}
res.b = (int*)malloc(sizeof(int));
}
// ============================================================================
// Entry Point (Selectively uncomment any bug function to trigger/test)
// ============================================================================
int main(void) {
bug105_calloc_assumption_mismatch();
bug106_aligned_alloc_size_mismatch();
bug107_posix_memalign_uninit_out();
bug108_getcwd_malloc_leak();
bug109_asprintf_error_free();
bug110_getline_eof_leak();
bug111_memstream_unflushed_read();
bug112_memstream_buffer_leak();
bug113_fmemopen_no_null();
bug114_realpath_fixed_buffer();
bug115_realpath_malloc_leak();
bug116_scandir_partial_leak();
bug117_opendir_leak(1);
bug118_readdir_after_closedir();
bug119_regcomp_leak();
bug120_regfree_on_failed_compile();
bug121_shmat_detach_leak();
bug122_shmat_null_check();
bug123_epoll_union_overwrite();
bug125_nested_fam_overlap();
bug126_bitfield_memset_partial();
bug127_network_buffer_cast();
bug128_packed_member_addr();
bug131_tls_leak();
bug132_tls_stack_ptr();
bug133_destroy_locked_mutex();
bug134_copy_mutex_by_value();
bug135_copy_file_struct();
bug136_setvbuf_stack_scope();
bug137_freopen_old_pointer();
bug138_tmpnam_static_overwrite();
bug139_strerror_static_overwrite();
bug140_localtime_static_overwrite();
bug141_strtok_r_freed_buffer();
bug142_strtol_null_endptr();
bug143_sprintf_off_by_one();
bug144_scanf_unbounded();
bug145_scanf_char_as_string();
bug146_wchar_malloc_bytes();
bug147_wmemcpy_byte_count();
bug148_recursive_static(3);
bug149_unsequenced_pointer_write();
bug150_deref_before_null_check(NULL);
bug151_null_pointer_arithmetic();
bug152_past_struct_cast();
bug153_subobject_overflow();
bug155_vla_backward_goto();
bug156_setjmp_clobber();
bug157_longjmp_dead_frame();
bug159_realloc_breaks_alignment();
bug160_mismatched_allocator_free();
bug161_free_while_linked();
bug162_free_stack_sentinel();
bug163_rehash_freed_key();
bug164_bst_dangling_parent();
bug166_goto_fallthrough_double_free(1);
bug167_goto_uninit_free(1);
bug168_macro_double_eval_leak();
bug169_macro_no_do_while(0);
bug170_pointer_truncation_32bit();
bug171_ptrdiff_overflow();
bug172_unsigned_index_underflow();
bug173_memmove_shift_overflow();
bug178_atexit_uaf();
bug179_zero_capacity_growth();
bug180_jagged_array_memset();
bug181_fn_ptr_mismatch();
bug182_mmap_freed_with_free();
bug183_malloc_freed_with_munmap();
bug184_strcat_loop_overflow();
bug185_memchr_null_sub();
bug186_write_const_global();
bug187_compound_literal_loop_alias();
bug188_double_close_fd();
bug189_fdopen_fail_leak();
bug190_fclose_and_close_fd();
bug191_zero_len_null_memcpy();
bug192_cleanup_clobbers_errno();
bug193_memcpy_wrong_struct_size();
bug194_void_ptr_arithmetic();
bug195_sso_memcpy_dangling();
bug196_wrong_indirection_free();
bug197_unaligned_int_to_ptr();
bug199_realloc_zero_ub();
bug200_shift_ub_array_index();
bug201_destroy_active_condvar();
bug203_sizeof_ptr_instead_of_pointee();
bug204_partial_struct_cleanup(1);
return 0;
}
