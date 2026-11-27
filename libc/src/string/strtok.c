/* Namespaced strtok with per-thread, per-tokenization state.
 *
 * Host strtok() keeps one process-global hidden save pointer, so two threads
 * (or two interleaved loops) corrupt each other's tokenization and a NULL
 * delimiter crashes inside the host library.  We keep a small set of
 * strtok_r save pointers in thread-local storage, keyed by the delimiter
 * identity supplied when the sequence started.  A fresh `str` starts a new
 * sequence; `str == NULL` continues the most recent active sequence whose
 * delimiter matches.  Valid single-loop behaviour is unchanged.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "../../log/log.h"
#include <errno.h>
#include <string.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define CPLUS_STRTOK_TLS _Thread_local
#else
/* clang/gcc accept __thread in C99 (the mode this libc is built in). */
#define CPLUS_STRTOK_TLS __thread
#endif

#define CPLUS_STRTOK_SEQUENCES 8u

typedef struct cplus_strtok_state_s {
    const char *delim;
    char *save;
} cplus_strtok_state;

static CPLUS_STRTOK_TLS cplus_strtok_state g_states[CPLUS_STRTOK_SEQUENCES];
static CPLUS_STRTOK_TLS unsigned g_next_sequence;
static CPLUS_STRTOK_TLS unsigned g_current_sequence;

char *cplus_strtok(char *str, const char *delim)
{
    unsigned i;

    cplus_libc_log("strtok", "enter");
    if (delim == NULL) {
        errno = EINVAL;
        return NULL;
    }

    if (str != NULL) {
        /* Reuse a completed slot, otherwise rotate through the bounded set so
         * an interleaved loop cannot evict the sequence it is still using. */
        for (i = 0u; i < CPLUS_STRTOK_SEQUENCES; ++i) {
            if (g_states[i].save == NULL) break;
        }
        if (i == CPLUS_STRTOK_SEQUENCES) {
            i = g_next_sequence++ % CPLUS_STRTOK_SEQUENCES;
        }
        g_states[i].delim = delim;
        g_states[i].save = NULL;
        g_current_sequence = i;
        return strtok_r(str, delim, &g_states[i].save);
    }

    /* Continuation: prefer the most recently started sequence, then fall back
     * to a still-active sequence with the same delimiter (nested loops). */
    {
        cplus_strtok_state *s = &g_states[g_current_sequence];
        if (s->save != NULL && s->delim == delim)
            return strtok_r(NULL, delim, &s->save);
    }
    for (i = CPLUS_STRTOK_SEQUENCES; i > 0u; --i) {
        cplus_strtok_state *s = &g_states[i - 1u];
        if (s->save != NULL && s->delim == delim) {
            g_current_sequence = i - 1u;
            return strtok_r(NULL, delim, &s->save);
        }
    }
    errno = EINVAL; /* no matching active tokenization */
    return NULL;
}
