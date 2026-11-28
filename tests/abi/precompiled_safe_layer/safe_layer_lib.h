#ifndef SAFE_LAYER_LIB_H
#define SAFE_LAYER_LIB_H

/* Plain-C ABI contract shared by the precompiled archive and its callers.
 * Deliberately contains no C+ syntax and includes no C+/SafeCRT headers, so
 * it can be compiled by /usr/bin/clang with -std=c11 and no extra flags. */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int safe_layer_version(void);
int safe_layer_mul(int a, int b);
int safe_layer_checksum(const unsigned char *data, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* SAFE_LAYER_LIB_H */
