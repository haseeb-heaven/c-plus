/* Deterministic regression for issue #210: cplus_pthread_* wrappers must
 * validate required pointer arguments and return EINVAL instead of forwarding
 * NULL to host pthread_* (SIGSEGV/UB). Optional NULL arguments stay legal.
 *
 * Output: "pthread_null_attack: <checks> <failures>".
 */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>

int cplus_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int cplus_pthread_join(pthread_t, void **);
int cplus_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int cplus_pthread_mutex_lock(pthread_mutex_t *);
int cplus_pthread_mutex_unlock(pthread_mutex_t *);
int cplus_pthread_mutex_destroy(pthread_mutex_t *);
int cplus_pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int cplus_pthread_cond_signal(pthread_cond_t *);
int cplus_pthread_cond_broadcast(pthread_cond_t *);
int cplus_pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
int cplus_pthread_cond_destroy(pthread_cond_t *);

static int g_checks;
static int g_failures;

#define REFUSE(call, name)                                                     \
    do {                                                                       \
        int rc_;                                                               \
        errno = 0;                                                             \
        rc_ = (call);                                                          \
        ++g_checks;                                                            \
        if (rc_ != EINVAL || errno != EINVAL) {                                \
            ++g_failures;                                                      \
            fprintf(stderr, "FAIL: %s (rc=%d errno=%d)\n", (name), rc_, errno); \
        }                                                                      \
    } while (0)

static void *worker(void *arg) { (void)arg; return NULL; }

int main(void)
{
    pthread_mutex_t m;
    pthread_cond_t c;
    pthread_t t;

    REFUSE(cplus_pthread_create(NULL, NULL, worker, NULL), "create NULL thread");
    REFUSE(cplus_pthread_create(&t, NULL, NULL, NULL), "create NULL start");
    REFUSE(cplus_pthread_mutex_init(NULL, NULL), "mutex_init NULL");
    REFUSE(cplus_pthread_mutex_lock(NULL), "mutex_lock NULL");
    REFUSE(cplus_pthread_mutex_unlock(NULL), "mutex_unlock NULL");
    REFUSE(cplus_pthread_mutex_destroy(NULL), "mutex_destroy NULL");
    REFUSE(cplus_pthread_cond_init(NULL, NULL), "cond_init NULL");
    REFUSE(cplus_pthread_cond_signal(NULL), "cond_signal NULL");
    REFUSE(cplus_pthread_cond_broadcast(NULL), "cond_broadcast NULL");
    REFUSE(cplus_pthread_cond_wait(NULL, &m), "cond_wait NULL cond");

    /* Valid calls with optional NULL args still work (no over-validation). */
    if (cplus_pthread_mutex_init(&m, NULL) == 0) {
        REFUSE(cplus_pthread_cond_wait(&c, NULL), "cond_wait NULL mutex");
        if (cplus_pthread_cond_init(&c, NULL) == 0) {
            if (cplus_pthread_create(&t, NULL, worker, NULL) == 0) {
                if (cplus_pthread_join(t, NULL) != 0) {
                    ++g_checks; ++g_failures;
                    fprintf(stderr, "FAIL: join with NULL retval\n");
                } else { ++g_checks; }
            }
            if (cplus_pthread_cond_destroy(&c) != 0) {
                ++g_checks; ++g_failures;
                fprintf(stderr, "FAIL: cond_destroy valid\n");
            } else { ++g_checks; }
        }
        if (cplus_pthread_mutex_destroy(&m) != 0) {
            ++g_checks; ++g_failures;
            fprintf(stderr, "FAIL: mutex_destroy valid\n");
        } else { ++g_checks; }
    }
    printf("pthread_null_attack: %d %d\n", g_checks, g_failures);
    return g_failures != 0;
}
