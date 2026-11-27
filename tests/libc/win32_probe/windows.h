/* Minimal <windows.h> stub for the deterministic host-side probe of the
 * Windows datastack-TLS thread-exit branch. It is not a Windows SDK
 * substitute: it declares only the API the branch uses and records its
 * arguments so the host test can assert them. The companion script also
 * cross-compiles the real translation unit against the genuine MinGW SDK. */
#ifndef CPLUS_TEST_WINDOWS_H
#define CPLUS_TEST_WINDOWS_H

#include <stddef.h>

#define MEM_RELEASE 0x8000u

#ifdef __cplusplus
extern "C" {
#endif
extern int cplus_probe_vf_calls;
extern void *cplus_probe_vf_address;
extern size_t cplus_probe_vf_size;
extern unsigned cplus_probe_vf_flags;
#ifdef __cplusplus
}
#endif

#define VirtualFree(address, size, flags)                                     \
    (++cplus_probe_vf_calls, cplus_probe_vf_address = (void *)(address),       \
     cplus_probe_vf_size = (size_t)(size),                                    \
     cplus_probe_vf_flags = (unsigned)(flags), 1)

#endif /* CPLUS_TEST_WINDOWS_H */
