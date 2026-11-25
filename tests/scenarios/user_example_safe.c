/* The user-supplied 10-case program, compiled with ORDINARY C calls.
 *
 * Default:      C+ SafeCRT layer  (stdlib/safe/safe_c.h)
 * -DPLAIN_LIBC: plain libc layer  (cplus_libc_safe.h)
 *
 * No `_s` spelling appears in the test bodies. Tests 3 and 5 are kept
 * commented exactly as supplied; a companion file exercises them enabled.
 */
#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#else
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static void test_1(void) {
    volatile uint64_t input = UINT64_C(0x100000001);
    uint32_t count = (uint32_t)input;
    size_t bytes = count * 4u;
    unsigned char *p = malloc(bytes);
    if (!p) return;
    memset(p, 0x41, (size_t)count * 4);
    free(p);
}

static void test_2(void) {
    char *owner = malloc(16);
    if (!owner) return;
    char *alias = owner;
    memset(owner, 'A', 16);
    char *grown = realloc(owner, 1024 * 1024);
    if (!grown) { free(owner); return; }
    owner = grown;
    alias[0] = 'X';
    free(owner);
}

static void test_3(void) {
    int *a = malloc(64);
    if (!a) return;
    int *b = a;
    free(a);
    free(b); /* must be refused by the checked free */
}

struct Record { size_t length; unsigned char data[]; };
static void test_4(void) {
    volatile size_t input = 128;
    size_t length = input;
    struct Record *r = malloc(sizeof(*r) + 4);
    if (!r) return;
    r->length = length;
    memset(r->data, 0x41, r->length);
    free(r);
}

static void test_5(void) {
    char *p = malloc(32);
    if (!p) return;
    memcpy(p, "abcdefghijklmnop", 16);
    memcpy(p + 4, p, 12); /* overlap: must be refused or handled */
    free(p);
}

static char *escaped_pointer;
static void create_local(void) {
    char local[64];
    memset(local, 'A', sizeof(local));
    escaped_pointer = local;
}
static void test_6(void) {
    create_local();
    escaped_pointer[0] = 'X'; /* genuine raw-C UB: compiler-layer issue */
}

static void test_7(const char *input) {
    char destination[16];
    strcpy(destination, input);
    puts(destination);
}

static void test_8(void) {
    char *p = malloc(8);
    if (!p) return;
    memset(p, 'A', 8);
    volatile size_t length = strlen(p); /* raw strlen on unterminated buffer: UB */
    (void)length;
    free(p);
}

struct Credentials { uint32_t id; uint32_t permissions; char name[32]; };
static void test_9(void) {
    struct Credentials *c = malloc(sizeof(*c));
    if (!c) return;
    c->id = 100;
    strcpy(c->name, "user");
    if (c->permissions & 1u) puts("privileged"); /* uninitialized read: UB */
    free(c);
}

static void test_10(const char *arg) {
    size_t count = (size_t)strtoull(arg, NULL, 10);
    size_t element_size = 32;
    size_t bytes = count * element_size;
    char *p = malloc(bytes);
    if (!p) return;
    for (size_t i = 0; i < count; i++)
        memset(p + i * element_size, 0x41, element_size);
    free(p);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s test_number [input]\n", argv[0]);
        return 1;
    }
    switch (atoi(argv[1])) {
        case 1: test_1(); break;
        case 2: test_2(); break;
        case 3: test_3(); break;
        case 4: test_4(); break;
        case 5: test_5(); break;
        case 6: test_6(); break;
        case 7: test_7(argc > 2 ? argv[2] : "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"); break;
        case 8: test_8(); break;
        case 9: test_9(); break;
        case 10: test_10(argc > 2 ? argv[2] : "1000000000000000"); break;
        default: fprintf(stderr, "Invalid test number\n"); return 1;
    }
    return 0;
}
