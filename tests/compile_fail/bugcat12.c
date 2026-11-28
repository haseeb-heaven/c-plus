/* Bug 385: seqlock with a non-atomic sequence and no reader acquire fence. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

static int seq = 0;
static int x = 0;
static int y = 0;

static void *writer(void *arg) {
    (void)arg;
    seq++;                             /* unsynchronised sequence counter */
    x++;
    y++;
    seq++;
    return NULL;
}

int main(void) {
    pthread_t a, b;
    pthread_create(&a, NULL, writer, NULL);
    pthread_create(&b, NULL, writer, NULL);
    int s1, sx, sy, s2;
    do {
        s1 = seq;                      /* plain load: no atomicity/barrier */
        sx = x;                        /* no acquire fence: may be torn */
        sy = y;
        s2 = seq;
    } while (s1 != s2 || (s1 & 1));
    printf("snapshot=%d,%d\n", sx, sy);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    return 0;
}
