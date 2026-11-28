/* Bug 365: pthread_cond_wait without a predicate loop (plus unsynchronised
 * mutation of the predicate by the notifier threads). */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int events = 0;

static void *notifier(void *arg) {
    (void)arg;
    events++;                          /* predicate written without mutex */
    pthread_cond_signal(&cv);
    return NULL;
}

int main(void) {
    pthread_t a, b;
    pthread_create(&a, NULL, notifier, NULL);
    pthread_create(&b, NULL, notifier, NULL);
    pthread_mutex_lock(&mu);
    if (events == 0)                   /* must be a while loop */
        pthread_cond_wait(&cv, &mu);
    printf("events=%d\n", events);
    pthread_mutex_unlock(&mu);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    return 0;
}
