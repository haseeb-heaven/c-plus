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

typedef enum {
    CPLUS_SAFE_CONSTRAINT_IGNORE = 0,
    CPLUS_SAFE_CONSTRAINT_ABORT  = 1
} cplus_safe_constraint_mode;

void cplus_safe_set_constraint_mode(cplus_safe_constraint_mode mode);
cplus_safe_constraint_mode cplus_safe_get_constraint_mode(void);
#ifdef __cplusplus
}
#endif
#endif
