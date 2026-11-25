/* Bug 375: ABA in a lock-free stack whose 32-bit generation tag wraps. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

struct node {
    int value;
    uint32_t gen;
    struct node *next;
};

static _Atomic(struct node *) top = NULL;
static int pop_count = 0;

static void *popper(void *arg) {
    (void)arg;
    struct node *old = atomic_load_explicit(&top, memory_order_relaxed);
    struct node *next = (old != NULL) ? old->next : NULL;
    pop_count++;                       /* unsynchronised shared state */
    atomic_compare_exchange_weak_explicit(&top, &old, next,
                                          memory_order_acq_rel,
                                          memory_order_relaxed);
    return NULL;
}

int main(void) {
    pthread_t a, b;
    pthread_create(&a, NULL, popper, NULL);
    pthread_create(&b, NULL, popper, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    printf("pops=%d\n", pop_count);
    return 0;
}
