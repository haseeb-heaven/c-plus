/* Deterministic host-side probe for the Windows branch of the thread-local
 * DataStack destructor. The real libc/src/stdlib/datastack_tls.c translation
 * unit is included with -D_WIN32 and a stub <windows.h> (see win32_probe/),
 * so the VirtualFree/MEM_RELEASE path that leaks on worker-thread exit is
 * actually executed on a non-Windows host. The companion runner also
 * cross-compiles that same translation unit with MinGW against the real SDK.
 *
 * Output: "datastack_tls_win32_release: <checks> <failures>".
 */
#define _DEFAULT_SOURCE 1
#define _DARWIN_C_SOURCE 1
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -I tests/libc/win32_probe precedes system includes for <windows.h> only. */
#include "../../../libc/src/stdlib/datastack_tls.c"

int cplus_probe_vf_calls;
void *cplus_probe_vf_address;
size_t cplus_probe_vf_size;
unsigned cplus_probe_vf_flags;

static int g_checks;
static int g_failures;

#define CHECK(cond, name)                                                      \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) { ++g_failures; fprintf(stderr, "FAIL: %s\n", (name)); }  \
    } while (0)

static void reset_probe(void)
{
    cplus_probe_vf_calls = 0;
    cplus_probe_vf_address = NULL;
    cplus_probe_vf_size = (size_t)-1;
    cplus_probe_vf_flags = 0u;
    memset(&stdlib_tls_datastack, 0, sizeof stdlib_tls_datastack);
}

int main(void)
{
    unsigned char *reservation;
    unsigned char *heap_block;
    unsigned long before;

    reservation = (unsigned char *)malloc(64u);
    heap_block = (unsigned char *)malloc(64u);
    CHECK(reservation != NULL && heap_block != NULL, "probe allocations");
    if (reservation == NULL || heap_block == NULL) {
        free(reservation);
        free(heap_block);
        printf("datastack_tls_win32_release: %d %d\n", g_checks, g_failures + 1);
        return 1;
    }

    /* VM-backed (backing == 1): the recorded reservation must be released
     * through VirtualFree with MEM_RELEASE and dwSize == 0. */
    reset_probe();
    stdlib_tls_datastack.base = reservation;
    stdlib_tls_datastack.top = reservation;
    stdlib_tls_datastack.capacity = 64u; /* recorded reservation size */
    stdlib_tls_datastack.committed = 64u;
    stdlib_tls_datastack.generation = 7u;
    stdlib_tls_datastack.backing = 1;
    stdlib_tls_datastack.page = 4096u;
    before = cplus_datastack_tls_reclaim_count();
    datastack_tls_destructor(&stdlib_tls_datastack);
    CHECK(cplus_probe_vf_calls == 1, "one VirtualFree for VM-backed TLS");
    CHECK(cplus_probe_vf_address == reservation,
          "VirtualFree got the recorded reservation base");
    CHECK(cplus_probe_vf_flags == MEM_RELEASE, "VirtualFree used MEM_RELEASE");
    CHECK(cplus_probe_vf_size == 0u,
          "VirtualFree dwSize is zero as Win32 requires");
    CHECK(cplus_datastack_tls_reclaim_count() == before + 1u,
          "VM reclaim counted once");
    CHECK(stdlib_tls_datastack.base == NULL &&
          stdlib_tls_datastack.capacity == 0u &&
          stdlib_tls_datastack.backing == 0,
          "VM descriptor cleared after release");
    free(reservation); /* the stub does not actually free the region */

    /* A descriptor with no recorded reservation must not call VirtualFree,
     * matching the non-Windows guard. */
    reset_probe();
    reservation = (unsigned char *)malloc(64u);
    CHECK(reservation != NULL, "empty-reservation probe allocation");
    if (reservation != NULL) {
        stdlib_tls_datastack.base = reservation;
        stdlib_tls_datastack.capacity = 0u;
        stdlib_tls_datastack.backing = 1;
        datastack_tls_destructor(&stdlib_tls_datastack);
        CHECK(cplus_probe_vf_calls == 0,
              "no VirtualFree without a recorded reservation");
        free(reservation);
    }

    /* Heap-backed (backing == 0) must stay on cplus_release, untouched by the
     * new Windows branch. */
    reset_probe();
    stdlib_tls_datastack.base = heap_block;
    stdlib_tls_datastack.top = heap_block;
    stdlib_tls_datastack.capacity = 64u;
    stdlib_tls_datastack.committed = 64u;
    stdlib_tls_datastack.backing = 0;
    stdlib_tls_datastack.page = 4096u;
    datastack_tls_destructor(&stdlib_tls_datastack);
    CHECK(cplus_probe_vf_calls == 0, "heap backing never calls VirtualFree");
    CHECK(stdlib_tls_datastack.base == NULL, "heap descriptor cleared");

    printf("datastack_tls_win32_release: %d %d\n", g_checks, g_failures);
    return g_failures != 0;
}
