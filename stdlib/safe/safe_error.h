#ifndef CPLUS_SAFE_ERROR_H
#define CPLUS_SAFE_ERROR_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define CPLUS_SAFE_OK        0
#define CPLUS_SAFE_EINVAL    22
#define CPLUS_SAFE_ENOMEM    12
#define CPLUS_SAFE_EOVERFLOW 75
#define CPLUS_SAFE_ERANGE    34
#define CPLUS_SAFE_EUNAVAIL  95

/*
 * Safeclib constraint-handler mode for C+ safe adapters.
 *
 * IGNORE (default): constraint violations are reported through API return
 * values/defined error behavior; execution continues.
 * ABORT: constraint violations invoke the abort handler and terminate the
 * process. Use only where process-fatal failure is intended.
 *
 * The mode is applied to both mem and str constraint handlers in the
 * vendored safeclib backend.
 */
typedef enum {
    CPLUS_SAFE_CONSTRAINT_IGNORE = 0,
    CPLUS_SAFE_CONSTRAINT_ABORT  = 1
} cplus_safe_constraint_mode;

/* Sets constraint mode; invalid values fall back to IGNORE. */
void cplus_safe_set_constraint_mode(cplus_safe_constraint_mode mode);
/* Returns the current constraint mode. */
cplus_safe_constraint_mode cplus_safe_get_constraint_mode(void);
#ifdef __cplusplus
}
#endif
#endif
