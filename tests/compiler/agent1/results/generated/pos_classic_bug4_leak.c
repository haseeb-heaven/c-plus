#if defined(CPLUS_AUTO_RUNTIME)
extern void cplus_set_auto_runtime(int);
static void __attribute__((constructor)) __cplus_auto_runtime_init(void) {
  cplus_set_auto_runtime(1);
}
#endif
extern void cplus_check_nonnull(const void *, const char *, int, const char *);
#if defined(__GNUC__) || defined(__clang__)
#define CPLUS_NONNULL(P) ({ __typeof__(P) __cplus_nn_p = (P); \
  cplus_check_nonnull((const void *)__cplus_nn_p, __FILE__, __LINE__, #P); \
  __cplus_nn_p; })
#else
#define CPLUS_NONNULL(P) \
  (cplus_check_nonnull((const void *)(P), __FILE__, __LINE__, #P), (P))
#endif
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/classic_bug4_leak.c"
/* Bug 4: leak on early return.  A C+ resource is cleaned up on every exit. */
#include "cplus/cplus.h"

 struct Buffer {
    int id;
};
typedef struct Buffer Buffer;
#line 8 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/classic_bug4_leak.c"


void Buffer_drop(Buffer *self) {
    if (CPLUS_NONNULL(self)->id != 0) {
        print("drop", CPLUS_NONNULL(self)->id);
        CPLUS_NONNULL(self)->id = 0;
    }
}

static void process(int trigger_error) {
    Buffer b = {7};
    if (trigger_error)
        { Buffer_drop(&b); return; }
    (void)b;
Buffer_drop(&b); 
#line 24 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/classic_bug4_leak.c"
}

int main(void) {
    process(1);
    print("done");
    return 0;
}
