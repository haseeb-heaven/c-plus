#ifndef CPLUS_SAFE_EXCEPT_H
#define CPLUS_SAFE_EXCEPT_H
/* C+ unified exception handling (Agent 2 runtime integration).
 *
 * Single engine: vendored exceptions4c (Apache-2.0, guillermocalvo/exceptions4c,
 * pinned 9f7879589936a53a037963502a25d8e07738a66b) in third_party/exceptions4c/.
 * This header does NOT implement its own engine; it re-exports the upstream
 * TRY / CATCH / CATCH_ALL / FINALLY / THROW macros and adds:
 *   - thread-local context supplier (each thread gets its own exception context)
 *   - RETHROW (re-throw the currently caught exception)
 *   - CPLUS_CHECK / CPLUS_THROW_IF (bridge structured errno_t -> exception)
 *
 * Policy: library APIs return structured errors (errno_t / cplus status) by
 * default. Exceptions are opt-in: wrap calls in TRY/CATCH, or use the bridge
 * macros to propagate a structured failure as an exception.
 */
#include "exceptions4c.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Install a thread-local exception-context supplier. Call once per process
 * (idempotent). Without it, exceptions4c uses a single global context that is
 * NOT thread-safe. */
void cplus_except_init(void);

/* Re-throw the exception currently being handled (inside CATCH / CATCH_ALL). */
#define RETHROW()                                                             \
    do {                                                                      \
        const struct e4c_exception *_cplus_e = e4c_get_exception();           \
        EXCEPTIONS4C_LONG_JUMP(e4c_throw(_cplus_e->type, _cplus_e->name,      \
                                         EXCEPTIONS4C_DEBUG, "%s",            \
                                         _cplus_e->message));                 \
    } while (0)

/* Bridge: if `cond` is false, throw `type` with a formatted message. */
#define CPLUS_CHECK(cond, type, ...)                                          \
    do { if (!(cond)) { THROW(type, __VA_ARGS__); } } while (0)

/* Bridge: if structured `status` != 0, throw `type` with a formatted message. */
#define CPLUS_THROW_IF(status, type, ...)                                     \
    do { if ((status) != 0) { THROW(type, __VA_ARGS__); } } while (0)

#if defined(__cplusplus)
}
#endif
#endif
