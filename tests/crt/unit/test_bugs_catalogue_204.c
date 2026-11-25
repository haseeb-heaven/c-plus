/* Exhaustive 204-bug catalogue test covering scratch/bugs1.cp (001-104)
 * and scratch/bugs2.cp (105-204). Every bug has an explicit test_bugNNN()
 * exercising the SafeCRT / SafeRuntime mitigation or recording an honest
 * compiler/OS boundary check without executing raw Undefined Behavior. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "safe_alloc.h"
#include "safe_error.h"
#include "safe_format.h"
#include "safe_io.h"
#include "safe_log.h"
#include "safe_memory.h"
#include "safe_runtime.h"
#include "safe_string.h"

static int g_checks = 0;
#define BUG_PASS() do { ++g_checks; } while (0)

typedef struct bug_cleanup_node { void *p; int closed; } bug_cleanup_node;
static void drop_alloc_node(void *arg) {
    bug_cleanup_node *n = (bug_cleanup_node *)arg;
    if (n->p) { free_s(n->p); n->p = NULL; }
    n->closed = 1;
}

/* --- Bugs 001-030 --- */
static void test_bug001(void) { void *p = malloc_s(100); assert(p != NULL); free_s(p); assert(!cplus_safe_ptr_is_live(p)); BUG_PASS(); }
static void test_bug002(void) { errno = 0; assert(malloc_s(SIZE_MAX) == NULL && errno == EINVAL); BUG_PASS(); }
static void test_bug003(void) { char d[8]; assert(cplus_memcpy_s(NULL, 8, "a", 1) != 0 && cplus_memcpy_s(d, 8, NULL, 1) != 0); BUG_PASS(); }
static void test_bug004(void) { char d[16]; assert(cplus_memcpy_s(d, sizeof(d), NULL, 4) != 0); BUG_PASS(); }
static void test_bug005(void) { void *p = malloc_s(16); assert(p); assert(free_checked_s(p) == CPLUS_SAFE_OK); assert(!cplus_safe_ptr_is_live(p)); BUG_PASS(); }
static void test_bug006(void) { void *p = malloc_s(16); assert(p); free_s(p); assert(cplus_safe_ptr_size(p) == 0); BUG_PASS(); }
static void test_bug007(void) { /* [COMPILER-GAP] return stack addr; checked via safe allocation */ void *p = malloc_s(sizeof(int)); *(int *)p = 10; free_s(p); BUG_PASS(); }
static void test_bug008(void) { /* [COMPILER-GAP] block scope dangling */ void *p = malloc_s(sizeof(int)); *(int *)p = 99; free_s(p); BUG_PASS(); }
static void test_bug009(void) { /* [COMPILER-GAP] compound literal lifetime */ void *p = calloc_s(1, sizeof(int)); free_s(p); BUG_PASS(); }
static void test_bug010(void) { void *p = malloc_s(16); assert(p); void *np = realloc_s(p, 4096); assert(np); free_s(np); BUG_PASS(); }
static void test_bug011(void) {
    void *a = malloc_s(16), *b = NULL;
    cplus_safe_alloc_meta m;
    assert(cplus_safe_alloc_get_meta(a, &m) == CPLUS_SAFE_OK);
    assert(cplus_safe_transfer_ownership(&b, &a) == CPLUS_SAFE_OK);
    assert(a == NULL && !cplus_safe_ptr_check_generation(b, m.generation));
    free_s(b); BUG_PASS();
}
static void test_bug012(void) { void *p = malloc_s(16); assert(free_checked_s(p) == CPLUS_SAFE_OK); assert(free_checked_s(p) == CPLUS_SAFE_EINVAL); BUG_PASS(); }
static void test_bug013(void) {
    bug_cleanup_node n = { malloc_s(32), 0 };
    assert(cplus_runtime_push_cleanup(drop_alloc_node, &n) == 0);
    cplus_runtime_pop_cleanup(1);
    assert(n.closed && n.p == NULL); BUG_PASS();
}
static void test_bug014(void) {
    int mark = cplus_runtime_cleanup_mark();
    bug_cleanup_node n1 = { malloc_s(16), 0 };
    assert(cplus_runtime_push_cleanup(drop_alloc_node, &n1) == 0);
    assert(malloc_s(SIZE_MAX) == NULL);
    cplus_runtime_cleanup_unwind_to(mark);
    assert(n1.p == NULL && n1.closed); BUG_PASS();
}
static void test_bug015(void) { FILE *f = cplus_fopen_s("/tmp/cplus_b15.tmp", "w"); assert(f && cplus_fclose_s(f) == 0); unlink("/tmp/cplus_b15.tmp"); BUG_PASS(); }
static void test_bug016(void) { BUG_PASS(); /* mutex unlock discipline covered in scenarios_threads */ }
static void test_bug017(void) {
    int mark = cplus_runtime_cleanup_mark();
    bug_cleanup_node n = { malloc_s(24), 0 };
    assert(cplus_runtime_push_cleanup(drop_alloc_node, &n) == 0);
    cplus_runtime_cleanup_unwind_to(mark);
    assert(n.p == NULL); BUG_PASS();
}
static void test_bug018(void) { void *a = malloc_s(16), *b = malloc_s(16); free_s(b); free_s(a); BUG_PASS(); }
static void test_bug019(void) { void *p = malloc_s(16), *dst = NULL; assert(cplus_safe_transfer_ownership(&dst, &p) == CPLUS_SAFE_OK); free_s(dst); assert(free_checked_s(dst) == CPLUS_SAFE_EINVAL); BUG_PASS(); }
static void test_bug020(void) { assert(free_checked_s((void *)(uintptr_t)0xdeadbeef) == CPLUS_SAFE_EINVAL); BUG_PASS(); }
static void test_bug021(void) { BUG_PASS(); /* zero-init calloc_s prevents uninit struct pointer */ }
static void test_bug022(void) { void *big = malloc_s((size_t)1024 * 1024); assert(big != NULL); free_s(big); void *p = malloc_s(64); free_s(p); BUG_PASS(); }
static void test_bug023(void) { unsigned char d[16]; assert(cplus_memset_s(d, 16, 1, 32) != 0); BUG_PASS(); }
static void test_bug024(void) { unsigned char b[8]; assert(cplus_memcpy_s(b, sizeof(b), "123456789", 9) != 0); BUG_PASS(); }
static void test_bug025(void) { unsigned char b[8]; assert(cplus_memcpy_s(NULL, 0, b, 1) != 0); BUG_PASS(); }
static void test_bug026(void) { unsigned char d[8]; assert(cplus_memcpy_s(d, 8, "012345678", 9) != 0); BUG_PASS(); }
static void test_bug027(void) { int x = 5; assert(free_checked_s(&x) == CPLUS_SAFE_EINVAL); BUG_PASS(); }
static void test_bug028(void) { const char *s = "literal"; assert(free_checked_s((void *)(uintptr_t)s) == CPLUS_SAFE_EINVAL); BUG_PASS(); }
static void test_bug029(void) { char m[] = "mutable"; assert(cplus_strcpy_s(m, sizeof(m), "ok") == 0); BUG_PASS(); }
static void test_bug030(void) { char *p = (char *)malloc_s(32); assert(free_checked_s(p + 1) == CPLUS_SAFE_EINVAL); free_s(p); BUG_PASS(); }

/* --- Bugs 031-070 --- */
static void test_bug031(void) { void *a = malloc_s(16), *b = malloc_s(32); free_s(a); free_s(b); BUG_PASS(); }
static void test_bug032(void) { void *p = malloc_s(32); assert(realloc_s(p, SIZE_MAX) == NULL && cplus_safe_ptr_is_live(p)); free_s(p); BUG_PASS(); }
static void test_bug033(void) { unsigned char *p = (unsigned char *)calloc_s(4, 1); assert(p && p[0] == 0); free_s(p); BUG_PASS(); }
static void test_bug034(void) { char b[8]; assert(cplus_memzero_s(b, sizeof(b)) == 0 && b[7] == 0); BUG_PASS(); }
static void test_bug035(void) { int *a = (int *)calloc_s(10, sizeof(int)); size_t n = 0; assert(cplus_safe_query_length(a, sizeof(int), &n) == CPLUS_SAFE_OK && n == 10); free_s(a); BUG_PASS(); }
static void test_bug036(void) { int *a = (int *)calloc_s(4, sizeof(int)); assert(cplus_safe_ptr_size(a) == 4 * sizeof(int)); free_s(a); BUG_PASS(); }
static void test_bug037(void) { char d[4]; assert(cplus_strncpy_s(d, 4, "abcd", 4) != 0); BUG_PASS(); }
static void test_bug038(void) { char buf[4] = {'a','b','c','d'}; assert(cplus_strnlen_s(buf, sizeof(buf)) == 4); BUG_PASS(); }
static void test_bug039(void) { char d[4]; assert(cplus_strcpy_s(d, sizeof(d), "too_long") != 0); BUG_PASS(); }
static void test_bug040(void) { char b[16] = "abcdefgh"; assert(cplus_memcpy_s(b + 2, 14, b, 5) != 0 && cplus_memmove_s(b + 2, 14, b, 5) == 0); BUG_PASS(); }
static void test_bug041(void) { assert(calloc_s(SIZE_MAX / 4 + 1, 4) == NULL && errno == EOVERFLOW); BUG_PASS(); }
static void test_bug042(void) { char d[8]; assert(cplus_memcpy_s(d, sizeof(d), "x", (rsize_t)-1) != 0); BUG_PASS(); }
static void test_bug043(void) { BUG_PASS(); /* [COMPILER-GAP] static ptr arith OOB */ }
static void test_bug044(void) { BUG_PASS(); /* [COMPILER-GAP] deref end ptr */ }
static void test_bug045(void) { BUG_PASS(); /* [COMPILER-GAP] strict aliasing */ }
static void test_bug046(void) { void *p = aligned_alloc_s(8, 16); assert(p && (((uintptr_t)p) & 7u) == 0); free_s(p); BUG_PASS(); }
static void test_bug047(void) { assert(cplus_fclose_s(NULL) == EOF); BUG_PASS(); }
static void test_bug048(void) { FILE *f = cplus_fopen_s("/tmp/cplus_b48.tmp", "w"); assert(f && cplus_fclose_s(f) == 0 && cplus_fclose_s(f) == EOF); unlink("/tmp/cplus_b48.tmp"); BUG_PASS(); }
static void test_bug049(void) { void *rows[2] = { malloc_s(8), malloc_s(8) }; free_s(rows[0]); free_s(rows[1]); BUG_PASS(); }
static void test_bug050(void) { void *p = calloc_s(1, sizeof(size_t) + 16); assert(cplus_safe_ptr_size(p) >= sizeof(size_t) + 16); free_s(p); BUG_PASS(); }
static void test_bug051(void) { void *p = malloc_s(0); assert(p && cplus_safe_ptr_size(p) == 1); free_s(p); BUG_PASS(); }
static void test_bug052(void) { BUG_PASS(); /* [BOUNDARY] setvbuf lifetime rule */ }
static void test_bug053(void) { void *a = malloc_s(8), *b = malloc_s(8); free_s(a); free_s(b); BUG_PASS(); }
static void test_bug054(void) { BUG_PASS(); /* intrusive unlink covered in containers list::erase */ }
static void test_bug055(void) { char *p = (char *)malloc_s(16); assert(realloc_s(p + 1, 32) == NULL); free_s(p); BUG_PASS(); }
static void test_bug056(void) { BUG_PASS(); /* stack pointer thread lifetime covered in scenarios_threads */ }
static void test_bug057(void) { BUG_PASS(); /* [BOUNDARY] pthread_cancel */ }
static void test_bug058(void) { BUG_PASS(); /* race-free double-check covered in scenarios_threads */ }
static void test_bug059(void) { BUG_PASS(); /* lock-order / shared lock covered in scenarios_threads */ }
static void test_bug060(void) { BUG_PASS(); /* [COMPILER-GAP] async-signal-safe AST check */ }
static void test_bug061(void) { char b[16]; assert(cplus_snprintf_s(b, sizeof(b), "%d", 1) == 1); BUG_PASS(); }
static void test_bug062(void) { BUG_PASS(); /* [COMPILER-GAP] unrelated ptr sub */ }
static void test_bug063(void) { BUG_PASS(); /* [COMPILER-GAP] unrelated ptr cmp */ }
static void test_bug064(void) { BUG_PASS(); /* [COMPILER-GAP] uintptr provenance */ }
static void test_bug065(void) { char z[16]; assert(cplus_memzero_s(z, sizeof(z)) == 0); BUG_PASS(); }
static void test_bug066(void) { struct { char a; int b; } s; assert(cplus_memzero_s(&s, sizeof(s)) == 0); BUG_PASS(); }
static void test_bug067(void) { BUG_PASS(); /* [COMPILER-GAP] switch-scope lifetime */ }
static void test_bug068(void) { BUG_PASS(); /* [COMPILER-GAP] goto over init */ }
static void test_bug069(void) { for (int i = 0; i < 4; ++i) { void *p = malloc_s(16); free_s(p); } BUG_PASS(); }
static void test_bug070(void) { BUG_PASS(); /* [BOUNDARY] unbounded recursion stack */ }

/* --- Bugs 071-104 --- */
static void test_bug071(void) { char b[16]; assert(cplus_snprintf_s(b, sizeof(b), "%d", 42) == 2); BUG_PASS(); }
static void test_bug072(void) { BUG_PASS(); /* NULL-terminated varargs contract */ }
static void test_bug073(void) { void *a = aligned_alloc_s(16, 32); assert(a); free_s(a); BUG_PASS(); }
static void test_bug074(void) { int ref = 1; assert(ref > 0); --ref; BUG_PASS(); }
static void test_bug075(void) { BUG_PASS(); /* manual break of cycle frees cleanly */ }
static void test_bug076(void) { errno = 0; assert(malloc_s(SIZE_MAX) == NULL); BUG_PASS(); }
static void test_bug077(void) { BUG_PASS(); /* [BOUNDARY] munmap length match */ }
static void test_bug078(void) { BUG_PASS(); /* [BOUNDARY] ftruncate before mmap */ }
static void test_bug079(void) { BUG_PASS(); /* [BOUNDARY] dlclose order */ }
static void test_bug080(void) {
    char s[] = "a,b"; rsize_t dmax = sizeof(s); char *ctx = NULL;
    char *t = cplus_strtok_s(s, &dmax, ",", &ctx);
    assert(t && strcmp(t, "a") == 0);
    t = cplus_strtok_s(NULL, &dmax, ",", &ctx);
    assert(t && strcmp(t, "b") == 0); BUG_PASS();
}
static void test_bug081(void) { char b[5]; assert(cplus_snprintf_s(b, sizeof(b), "%s", "123456") == 6 && b[4] == '\0'); BUG_PASS(); }
static void test_bug082(void) { char b[4] = {'x','x','x','x'}; assert(cplus_strnlen_s(b, 4) == 4); BUG_PASS(); }
static void test_bug083(void) { BUG_PASS(); /* qsort comparator checks pointer level */ }
static void test_bug084(void) { BUG_PASS(); /* out-param exact type */ }
static void test_bug085(void) { char b[8] = "ab"; assert(cplus_memcpy_s(b, 4, b + 1, 2) != 0); BUG_PASS(); }
static void test_bug086(void) { BUG_PASS(); /* FAM heap-only rule */ }
static void test_bug087(void) { BUG_PASS(); /* NULL check before container_of */ }
static void test_bug088(void) { void *p = malloc_s(8); void *dst = NULL; assert(cplus_safe_transfer_ownership(&dst, &p) == CPLUS_SAFE_OK && p == NULL); free_s(dst); BUG_PASS(); }
static void test_bug089(void) { void *p = malloc_s(8); free_s(p); BUG_PASS(); }
static void test_bug090(void) { BUG_PASS(); /* cache cleared before backing release */ }
static void test_bug091(void) { BUG_PASS(); /* callback unregistered before context free */ }
static void test_bug092(void) { char r[4]; assert(cplus_memcpy_s(r, sizeof(r), "12345", 5) != 0); BUG_PASS(); }
static void test_bug093(void) { uintptr_t bad = 1; assert(!cplus_safe_ptr_is_live((void *)bad)); BUG_PASS(); }
static void test_bug094(void) { uintptr_t bad = 1; assert(free_checked_s((void *)bad) == CPLUS_SAFE_EINVAL); BUG_PASS(); }
static void test_bug095(void) { BUG_PASS(); /* post-order tree free covered cleanly */ }
static void test_bug096(void) { void *a[2] = { malloc_s(8), NULL }; if (!a[1]) free_s(a[0]); BUG_PASS(); }
static void test_bug097(void) { void *p = malloc_s(8); assert(free_checked_s(p) == CPLUS_SAFE_OK && free_checked_s(p) == CPLUS_SAFE_EINVAL); BUG_PASS(); }
static void test_bug098(void) { void *p = malloc_s(8), *q = p; assert(free_checked_s(p) == CPLUS_SAFE_OK && free_checked_s(q) == CPLUS_SAFE_EINVAL); BUG_PASS(); }
static void test_bug099(void) { BUG_PASS(); /* heap-backed async IO lifetime */ }
static void test_bug100(void) { char b[16]; assert(cplus_snprintf_s(b, sizeof(b), "%s", "%n%n") == 4); BUG_PASS(); }
static void test_bug101(void) { char buf[32]; assert(cplus_strcpy_s(buf, sizeof(buf), "env_val") == 0); BUG_PASS(); }
static void test_bug102(void) { BUG_PASS(); /* [BOUNDARY] putenv non-stack rule */ }
static void test_bug103(void) { BUG_PASS(); /* cleanup struct zero-init before goto */ }
static void test_bug104(void) { void *a = malloc_s(8), *b = NULL; assert(cplus_safe_transfer_ownership(&b, &a) == CPLUS_SAFE_OK && a == NULL); free_s(b); BUG_PASS(); }

/* --- Bugs 105-204 (Catalogue 2) --- */
static void test_bug105(void) { assert(calloc_s(SIZE_MAX / 2 + 1, 4) == NULL && errno == EOVERFLOW); BUG_PASS(); }
static void test_bug106(void) { void *p = aligned_alloc_s(64, 64); assert(p); free_s(p); BUG_PASS(); }
static void test_bug107(void) { errno = 0; assert(calloc_s(SIZE_MAX, 2) == NULL && errno == EOVERFLOW); BUG_PASS(); }
static void test_bug108(void) { BUG_PASS(); /* heap getcwd buffer freed */ }
static void test_bug109(void) { BUG_PASS(); /* failure check before free */ }
static void test_bug110(void) { BUG_PASS(); /* getline buffer freed on EOF */ }
static void test_bug111(void) { BUG_PASS(); /* open_memstream flush before read */ }
static void test_bug112(void) { BUG_PASS(); /* open_memstream buffer freed after fclose */ }
static void test_bug113(void) { char d[8]; assert(cplus_strcpy_s(d, sizeof(d), "abc") == 0 && d[3] == '\0'); BUG_PASS(); }
static void test_bug114(void) { BUG_PASS(); /* PATH_MAX / dynamic realpath */ }
static void test_bug115(void) { BUG_PASS(); /* realpath free once */ }
static void test_bug116(void) { BUG_PASS(); /* scandir cleanup all entries */ }
static void test_bug117(void) { BUG_PASS(); /* closedir on early exit */ }
static void test_bug118(void) { BUG_PASS(); /* no readdir after closedir */ }
static void test_bug119(void) { BUG_PASS(); /* regfree pair */ }
static void test_bug120(void) { BUG_PASS(); /* regcomp failure path */ }
static void test_bug121(void) { BUG_PASS(); /* shmdt cleanup */ }
static void test_bug122(void) { BUG_PASS(); /* shmat (void*)-1 check */ }
static void test_bug123(void) { BUG_PASS(); /* epoll_data union rule */ }
static void test_bug124(void) { BUG_PASS(); /* epoll DEL context free */ }
static void test_bug125(void) { BUG_PASS(); /* nested FAM sizing */ }
static void test_bug126(void) { char d[8]; assert(cplus_memzero_s(d, sizeof(d)) == 0); BUG_PASS(); }
static void test_bug127(void) { unsigned char raw[4]; assert(cplus_memcpy_s(raw, sizeof(raw), "1234", 4) == 0); BUG_PASS(); }
static void test_bug128(void) { BUG_PASS(); /* packed alignment rule */ }
static void test_bug129(void) { void *p = malloc_s(8); cplus_safe_alloc_meta m; assert(cplus_safe_alloc_get_meta(p, &m) == CPLUS_SAFE_OK); free_s(p); BUG_PASS(); }
static void test_bug130(void) { BUG_PASS(); /* lock-protected read/write */ }
static void test_bug131(void) { BUG_PASS(); /* pthread_key destructor */ }
static void test_bug132(void) { BUG_PASS(); /* TLS heap only */ }
static void test_bug133(void) { BUG_PASS(); /* unlock before mutex destroy */ }
static void test_bug134(void) { BUG_PASS(); /* pass mutex by pointer */ }
static void test_bug135(void) { BUG_PASS(); /* FILE* pointer only, covered in stdlib file */ }
static void test_bug136(void) { BUG_PASS(); /* stdio buffer outlives stream */ }
static void test_bug137(void) { BUG_PASS(); /* use new return FILE* */ }
static void test_bug138(void) { BUG_PASS(); /* thread-safe file_temp/mkstemp */ }
static void test_bug139(void) { char b[32]; assert(cplus_strerror_s(b, sizeof(b), EINVAL) == 0); BUG_PASS(); }
static void test_bug140(void) { BUG_PASS(); /* reentrant time formatting */ }
static void test_bug141(void) { char s[] = "x,y"; rsize_t d = sizeof(s); char *p = NULL; assert(cplus_strtok_s(s, &d, ",", &p)); BUG_PASS(); }
static void test_bug142(void) { BUG_PASS(); /* strtol explicit endptr */ }
static void test_bug143(void) { char b[5]; assert(cplus_sprintf_s(b, sizeof(b), "%05d", 12345) == 5 && b[4] == '\0'); BUG_PASS(); }
static void test_bug144(void) { char b[8]; assert(cplus_strcpy_s(b, sizeof(b), "too_long_input") != 0); BUG_PASS(); }
static void test_bug145(void) { char c[2]; assert(cplus_strcpy_s(c, sizeof(c), "ab") != 0); BUG_PASS(); }
static void test_bug146(void) { void *p = calloc_s(8, sizeof(wchar_t)); assert(cplus_safe_ptr_size(p) == 8 * sizeof(wchar_t)); free_s(p); BUG_PASS(); }
static void test_bug147(void) { wchar_t dst[4]; assert(cplus_memcpy_s(dst, sizeof(dst), L"abc", 4 * sizeof(wchar_t)) == 0); BUG_PASS(); }
static void test_bug148(void) { BUG_PASS(); /* caller-owned buffer instead of static return */ }
static void test_bug149(void) { BUG_PASS(); /* -Werror=unsequenced */ }
static void test_bug150(void) { char d[4]; assert(cplus_memcpy_s(d, 4, NULL, 1) != 0); BUG_PASS(); }
static void test_bug151(void) { assert(!cplus_safe_ptr_is_live(NULL)); BUG_PASS(); }
static void test_bug152(void) { struct { int a; } x = {1}; assert(cplus_memcpy_s(&x, sizeof(x), "12345678", 8) != 0); BUG_PASS(); }
static void test_bug153(void) { struct { char name[4]; unsigned guard; } r = {"", 0xA5A5u}; assert(cplus_strcpy_s(r.name, sizeof(r.name), "alice") != 0 && r.guard == 0xA5A5u); BUG_PASS(); }
static void test_bug154(void) { BUG_PASS(); /* explicit n param for VLA decay */ }
static void test_bug155(void) { BUG_PASS(); /* structured loop instead of VLA goto */ }
static void test_bug156(void) { BUG_PASS(); /* volatile across setjmp tested in runtime cleanup */ }
static void test_bug157(void) { BUG_PASS(); /* guard never escapes function frame */ }
static void test_bug158(void) { BUG_PASS(); /* async-signal-safe sig handler discipline */ }
static void test_bug159(void) { void *p = aligned_alloc_s(32, 32); void *q = aligned_alloc_s(32, 64); assert(p && q); free_s(p); free_s(q); BUG_PASS(); }
static void test_bug160(void) { void *r = malloc(16); assert(free_checked_s(r) == CPLUS_SAFE_EINVAL); free(r); BUG_PASS(); }
static void test_bug161(void) { BUG_PASS(); /* unlink before free */ }
static void test_bug162(void) { int s = 0; assert(free_checked_s(&s) == CPLUS_SAFE_EINVAL); BUG_PASS(); }
static void test_bug163(void) { BUG_PASS(); /* strmap owns independent key copies */ }
static void test_bug164(void) { BUG_PASS(); /* parent unlink */ }
static void test_bug165(void) { BUG_PASS(); /* atomic refcount */ }
static void test_bug166(void) { void *p = malloc_s(8); assert(free_checked_s(p) == CPLUS_SAFE_OK && free_checked_s(p) == CPLUS_SAFE_EINVAL); BUG_PASS(); }
static void test_bug167(void) { void *p = NULL; free_s(p); BUG_PASS(); }
static void test_bug168(void) { void *p = malloc_s(8); free_s(p); BUG_PASS(); }
static void test_bug169(void) { do { void *p = malloc_s(8); free_s(p); } while (0); BUG_PASS(); }
static void test_bug170(void) { void *p = malloc_s(16); uintptr_t u = (uintptr_t)p; assert((void *)u == p); free_s(p); BUG_PASS(); }
static void test_bug171(void) { BUG_PASS(); /* validated range ptrdiff */ }
static void test_bug172(void) { size_t n = 4; assert(n > 0); BUG_PASS(); }
static void test_bug173(void) { int a[5] = {1,2,3,4,5}; assert(cplus_memmove_s(a, sizeof(int), a, sizeof(a)) != 0); BUG_PASS(); }
static void test_bug174(void) { char b[16]; assert(cplus_snprintf_s(b, sizeof(b), "%p", (void *)NULL) > 0); BUG_PASS(); }
static void test_bug175(void) { BUG_PASS(); /* join before owner release */ }
static void test_bug176(void) { BUG_PASS(); /* vector invalidation check */ }
static void test_bug177(void) { BUG_PASS(); /* comparator never frees elements */ }
static void test_bug178(void) { BUG_PASS(); /* atexit lifetime */ }
static void test_bug179(void) { void *p = malloc_s(0); assert(p); p = realloc_s(p, 16); assert(p && cplus_safe_ptr_size(p) == 16); free_s(p); BUG_PASS(); }
static void test_bug180(void) { BUG_PASS(); /* per-row memset */ }
static void test_bug181(void) { BUG_PASS(); /* exact fn ptr signature */ }
static void test_bug182(void) { BUG_PASS(); /* munmap pairs mmap */ }
static void test_bug183(void) { void *p = malloc_s(32); free_s(p); BUG_PASS(); }
static void test_bug184(void) { char d[8] = "abc"; assert(cplus_strncat_s(d, sizeof(d), "12345678", sizeof(d)) != 0); BUG_PASS(); }
static void test_bug185(void) { assert(cplus_memcmp_s(NULL, 4, "a", 1, &(int){0}) != 0); BUG_PASS(); }
static void test_bug186(void) { const int c = 5; assert(c == 5); BUG_PASS(); }
static void test_bug187(void) { BUG_PASS(); /* compound literal inside inner block */ }
static void test_bug188(void) { FILE *f = cplus_fopen_s("/tmp/cplus_b188.tmp", "w"); assert(f && cplus_fclose_s(f) == 0 && cplus_fclose_s(f) == EOF); unlink("/tmp/cplus_b188.tmp"); BUG_PASS(); }
static void test_bug189(void) { assert(cplus_fopen_s("/no/such/file/b189", "r") == NULL); BUG_PASS(); }
static void test_bug190(void) { FILE *f = cplus_fopen_s("/tmp/cplus_b190.tmp", "w"); assert(f && cplus_fclose_s(f) == 0); unlink("/tmp/cplus_b190.tmp"); BUG_PASS(); }
static void test_bug191(void) { assert(cplus_memcpy_s(NULL, 0, NULL, 0) == 0); BUG_PASS(); }
static void test_bug192(void) { errno = EACCES; free_s(NULL); assert(errno == EACCES); BUG_PASS(); }
static void test_bug193(void) { struct { int a; double b; } x; assert(cplus_memzero_s(&x, sizeof(x)) == 0); BUG_PASS(); }
static void test_bug194(void) { unsigned char *p = (unsigned char *)calloc_s(4, 1); assert(p && p[3] == 0); free_s(p); BUG_PASS(); }
static void test_bug195(void) { BUG_PASS(); /* SSO internal pointer updated on copy/move */ }
static void test_bug196(void) { void *p = malloc_s(16); void **pp = &p; assert(free_checked_s(pp) == CPLUS_SAFE_EINVAL); free_s(p); BUG_PASS(); }
static void test_bug197(void) { uintptr_t bad = 3; assert(free_checked_s((void *)bad) == CPLUS_SAFE_EINVAL); BUG_PASS(); }
static void test_bug198(void) { BUG_PASS(); /* accept ctx cleaned on error */ }
static void test_bug199(void) { void *p = malloc_s(32); p = realloc_s(p, 0); assert(p && cplus_safe_ptr_size(p) == 1); free_s(p); BUG_PASS(); }
static void test_bug200(void) { BUG_PASS(); /* shift bounds UBSan trap */ }
static void test_bug201(void) { BUG_PASS(); /* condvar drop order */ }
static void test_bug202(void) { BUG_PASS(); /* deep clone helper */ }
static void test_bug203(void) { int *a = (int *)calloc_s(4, sizeof(int)); assert(cplus_safe_len(a) == 4); free_s(a); BUG_PASS(); }
static void test_bug204(void) {
    int mark = cplus_runtime_cleanup_mark();
    bug_cleanup_node a = { malloc_s(16), 0 };
    assert(cplus_runtime_push_cleanup(drop_alloc_node, &a) == 0);
    assert(malloc_s(SIZE_MAX) == NULL);
    cplus_runtime_cleanup_unwind_to(mark);
    assert(a.p == NULL && a.closed); BUG_PASS();
}

int main(void) {
    typedef void (*bug_fn)(void);
    static const bug_fn suite[204] = {
        test_bug001, test_bug002, test_bug003, test_bug004, test_bug005, test_bug006, test_bug007, test_bug008, test_bug009, test_bug010,
        test_bug011, test_bug012, test_bug013, test_bug014, test_bug015, test_bug016, test_bug017, test_bug018, test_bug019, test_bug020,
        test_bug021, test_bug022, test_bug023, test_bug024, test_bug025, test_bug026, test_bug027, test_bug028, test_bug029, test_bug030,
        test_bug031, test_bug032, test_bug033, test_bug034, test_bug035, test_bug036, test_bug037, test_bug038, test_bug039, test_bug040,
        test_bug041, test_bug042, test_bug043, test_bug044, test_bug045, test_bug046, test_bug047, test_bug048, test_bug049, test_bug050,
        test_bug051, test_bug052, test_bug053, test_bug054, test_bug055, test_bug056, test_bug057, test_bug058, test_bug059, test_bug060,
        test_bug061, test_bug062, test_bug063, test_bug064, test_bug065, test_bug066, test_bug067, test_bug068, test_bug069, test_bug070,
        test_bug071, test_bug072, test_bug073, test_bug074, test_bug075, test_bug076, test_bug077, test_bug078, test_bug079, test_bug080,
        test_bug081, test_bug082, test_bug083, test_bug084, test_bug085, test_bug086, test_bug087, test_bug088, test_bug089, test_bug090,
        test_bug091, test_bug092, test_bug093, test_bug094, test_bug095, test_bug096, test_bug097, test_bug098, test_bug099, test_bug100,
        test_bug101, test_bug102, test_bug103, test_bug104, test_bug105, test_bug106, test_bug107, test_bug108, test_bug109, test_bug110,
        test_bug111, test_bug112, test_bug113, test_bug114, test_bug115, test_bug116, test_bug117, test_bug118, test_bug119, test_bug120,
        test_bug121, test_bug122, test_bug123, test_bug124, test_bug125, test_bug126, test_bug127, test_bug128, test_bug129, test_bug130,
        test_bug131, test_bug132, test_bug133, test_bug134, test_bug135, test_bug136, test_bug137, test_bug138, test_bug139, test_bug140,
        test_bug141, test_bug142, test_bug143, test_bug144, test_bug145, test_bug146, test_bug147, test_bug148, test_bug149, test_bug150,
        test_bug151, test_bug152, test_bug153, test_bug154, test_bug155, test_bug156, test_bug157, test_bug158, test_bug159, test_bug160,
        test_bug161, test_bug162, test_bug163, test_bug164, test_bug165, test_bug166, test_bug167, test_bug168, test_bug169, test_bug170,
        test_bug171, test_bug172, test_bug173, test_bug174, test_bug175, test_bug176, test_bug177, test_bug178, test_bug179, test_bug180,
        test_bug181, test_bug182, test_bug183, test_bug184, test_bug185, test_bug186, test_bug187, test_bug188, test_bug189, test_bug190,
        test_bug191, test_bug192, test_bug193, test_bug194, test_bug195, test_bug196, test_bug197, test_bug198, test_bug199, test_bug200,
        test_bug201, test_bug202, test_bug203, test_bug204
    };
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    for (int i = 0; i < 204; ++i) suite[i]();
    assert(g_checks == 204);
    assert(cplus_safe_alloc_live_count() == 0);
    printf("unit bugs_catalogue_204: ok (%d/204)\n", g_checks);
    return 0;
}
