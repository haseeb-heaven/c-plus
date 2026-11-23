/* Shared deterministic plumbing for the safec_layer_* fuzz harnesses.
 *
 * This header is deliberately included BEFORE stdlib/safe/safe_c.h by each
 * harness, so the helpers below use the raw ISO C names while the harness
 * bodies use the ordinary-name mapping macros from that header.
 *
 * No operation in this header writes outside a declared object. Canary
 * arrays surround every payload, and every "one-over" length is only ever
 * PASSED to the checked wrapper (never used for a raw out-of-bounds access).
 */
#ifndef SAFEC_LAYER_COMMON_H
#define SAFEC_LAYER_COMMON_H

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---------- fixed generator ---------- */
#define SC_SEED     UINT32_C(0x5AFEC1DE)
#define SC_MUL      UINT32_C(1664525)
#define SC_ADD      UINT32_C(1013904223)

static uint32_t sc_state_g = SC_SEED;

static void sc_seed(void) { sc_state_g = SC_SEED; }
static uint32_t sc_rnd(void) {
    sc_state_g = sc_state_g * SC_MUL + SC_ADD; /* uint32_t wrap is well-defined */
    return sc_state_g;
}

/* ---------- bounded lengths, canaries ---------- */
#define SC_LEN_MAX    512u
#define SC_CANARY_LEN 16u
#define SC_CANARY_BYTE 0xA5u
#define SC_ITERATIONS 200
#define SC_PASS_COUNT 2u

/* Length schedule: 0, exact-fit, one-over, then a bounded random length.
 * SC_LEN_MAX + 1 is the one-over shape; callers never use it for a raw write. */
static size_t sc_len_for(int iter) {
    switch (iter & 7) {
        case 0: return 0u;
        case 1: return SC_LEN_MAX;
        case 2: return SC_LEN_MAX + 1u;
        default: return (size_t)(sc_rnd() % (SC_LEN_MAX + 1u));
    }
}
static int sc_is_over(size_t n) { return n > SC_LEN_MAX; }

typedef struct {
    unsigned char pre[SC_CANARY_LEN];
    unsigned char data[SC_LEN_MAX];
    unsigned char post[SC_CANARY_LEN];
} sc_byte_obj;

typedef struct {
    unsigned char pre[SC_CANARY_LEN];
    char data[SC_LEN_MAX + 1u];   /* room for 512 chars + NUL */
    unsigned char post[SC_CANARY_LEN];
} sc_str_obj;

typedef struct {
    unsigned char pre[SC_CANARY_LEN];
    char data[SC_LEN_MAX + 2u];   /* room for a 513-char one-over source */
    unsigned char post[SC_CANARY_LEN];
} sc_src_obj;

static void sc_set_canaries(unsigned char *pre, unsigned char *post) {
    memset(pre, SC_CANARY_BYTE, SC_CANARY_LEN);
    memset(post, SC_CANARY_BYTE, SC_CANARY_LEN);
}
static void sc_check_canaries(const unsigned char *pre, const unsigned char *post) {
    size_t i;
    for (i = 0; i < SC_CANARY_LEN; ++i) {
        assert(pre[i] == SC_CANARY_BYTE);
        assert(post[i] == SC_CANARY_BYTE);
    }
}
static void sc_init_byte(sc_byte_obj *o) {
    size_t i;
    sc_set_canaries(o->pre, o->post);
    for (i = 0; i < SC_LEN_MAX; ++i) o->data[i] = (unsigned char)sc_rnd();
}
/* Deterministic string of length slen (<= SC_LEN_MAX + 1). */
static void sc_init_src(sc_src_obj *o, size_t slen) {
    size_t i;
    assert(slen <= SC_LEN_MAX + 1u);
    sc_set_canaries(o->pre, o->post);
    for (i = 0; i < sizeof o->data; ++i) o->data[i] = (char)0x5A;
    for (i = 0; i < slen; ++i) o->data[i] = (char)('a' + (int)(sc_rnd() % 26u));
    o->data[slen] = '\0';
}
static void sc_init_dest(sc_str_obj *o, size_t slen) {
    size_t i;
    assert(slen <= SC_LEN_MAX);
    sc_set_canaries(o->pre, o->post);
    for (i = 0; i < sizeof o->data; ++i) o->data[i] = (char)0x5A;
    for (i = 0; i < slen; ++i) o->data[i] = (char)('a' + (int)(sc_rnd() % 26u));
    o->data[slen] = '\0';
}

/* ---------- digest (FNV-1a 64; never hashes raw pointer addresses) ---------- */
static uint64_t sc_digest_g;
static void sc_digest_init(void) { sc_digest_g = UINT64_C(0xcbf29ce484222325); }
static void sc_hash(const void *p, size_t n) {
    const unsigned char *q = (const unsigned char *)p;
    while (n--) {
        sc_digest_g ^= (uint64_t)*q++;
        sc_digest_g *= UINT64_C(0x100000001b3);
    }
}
static void sc_hash_u64(uint64_t v) {
    int i;
    for (i = 0; i < 8; ++i) {
        unsigned char b = (unsigned char)(v >> (i * 8));
        sc_hash(&b, 1u);
    }
}
static void sc_hash_tag(const char *tag) { sc_hash(tag, strlen(tag) + 1u); }
static void sc_hash_sign(int v) {
    sc_hash_u64((uint64_t)((v > 0) - (v < 0) + 1)); /* -1/0/1 -> 0/1/2 */
}

/* ---------- quiet, non-aborting constraint handler ----------
 * The default ignore handler prints metadata to stdout; install a silent one
 * so the only stdout lines are the harness result lines. Violation trap is
 * disabled by each harness main() so one-over refusals remain observable
 * (errno / untouched canaries) instead of calling _exit(70). */
extern void cplus_sc_set_mem_constraint_handler_s_wrapper(void (*)(const char *, void *, int));
extern void cplus_sc_set_str_constraint_handler_s_wrapper(void (*)(const char *, void *, int));
static void sc_silent_handler(const char *msg, void *ptr, int error) {
    (void)msg; (void)ptr; (void)error;
}
static void sc_install_silent_handler(void) {
    cplus_sc_set_mem_constraint_handler_s_wrapper(sc_silent_handler);
    cplus_sc_set_str_constraint_handler_s_wrapper(sc_silent_handler);
}

/* ---------- stderr silencing (logger + violation diagnostics) ---------- */
static int sc_saved_stderr_g = -1;
static void sc_silence_stderr(void) {
    int devnull;
    if (getenv("SC_KEEP_STDERR") != NULL) return; /* debugging aid */
    sc_saved_stderr_g = dup(STDERR_FILENO);
    devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
        (void)dup2(devnull, STDERR_FILENO);
        (void)close(devnull);
    }
}
static void sc_restore_stderr(void) {
    if (sc_saved_stderr_g >= 0) {
        (void)dup2(sc_saved_stderr_g, STDERR_FILENO);
        (void)close(sc_saved_stderr_g);
        sc_saved_stderr_g = -1;
    }
}

#endif /* SAFEC_LAYER_COMMON_H */
