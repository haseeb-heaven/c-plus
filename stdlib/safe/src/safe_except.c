/* C+ thread-local exception context supplier (exceptions4c integration glue).
 * Single engine: third_party/exceptions4c (Apache-2.0). */
#include "../safe_except.h"
#include "../safe_log.h"
#include "../safe_stats.h"

#if defined(_MSC_VER)
#  define CPLUS_TLS __declspec(thread)
#else
#  define CPLUS_TLS __thread
#endif

static CPLUS_TLS struct e4c_context cplus_context;

static struct e4c_context *cplus_context_supplier(void) {
    return &cplus_context;
}

void cplus_except_init(void) {
    e4c_set_context_supplier(cplus_context_supplier);
    cplus_safe_log(CPLUS_SAFE_LOG_INFO, "safe_except", "tls context installed");
    cplus_safe_stat_record("except_init", 1);
}
