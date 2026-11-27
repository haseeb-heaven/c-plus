/* Internal declaration of the thread-local DataStack descriptor and the
 * pthread-key registration hook. Only libc/src translation units include it;
 * the public layout stays defined by stdlib/datastack.hp. */
#ifndef CPLUS_DATASTACK_TLS_INTERNAL_H
#define CPLUS_DATASTACK_TLS_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stdlib_datastack_tls {
    unsigned char *base;
    unsigned char *top;
    size_t capacity;
    size_t committed;
    uint64_t generation;
    int backing;
    size_t page;
} stdlib_datastack_tls;

extern __thread stdlib_datastack_tls stdlib_tls_datastack;

/* Idempotent per-thread registration of the exit destructor. Threads created
 * through cplus_pthread_create / cplus_thrd_create are registered at entry.
 * Returns 0 when the destructor is armed, non-zero if pthread_key_create or
 * pthread_setspecific failed. */
int cplus_datastack_tls_thread_register(void);

/* Diagnostic count of backing allocations released by thread-exit destructors. */
unsigned long cplus_datastack_tls_reclaim_count(void);

#ifdef __cplusplus
}
#endif
#endif /* CPLUS_DATASTACK_TLS_INTERNAL_H */
