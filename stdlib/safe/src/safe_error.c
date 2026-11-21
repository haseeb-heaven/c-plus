#include "../safe_error.h"
#include "../safe_log.h"
#include "../safe_stats.h"
#include <stdatomic.h>

extern void cplus_sc_abort_handler_s(const char *msg, void *ptr, int error);
extern void cplus_sc_ignore_handler_s(const char *msg, void *ptr, int error);
extern void cplus_sc_set_mem_constraint_handler_s_wrapper(void (*handler)(const char*, void*, int));
extern void cplus_sc_set_str_constraint_handler_s_wrapper(void (*handler)(const char*, void*, int));

static atomic_int g_mode = CPLUS_SAFE_CONSTRAINT_IGNORE;

void cplus_safe_set_constraint_mode(cplus_safe_constraint_mode mode) {
    if (mode != CPLUS_SAFE_CONSTRAINT_IGNORE && mode != CPLUS_SAFE_CONSTRAINT_ABORT) mode = CPLUS_SAFE_CONSTRAINT_IGNORE;
    atomic_store(&g_mode, (int)mode);
    void (*h)(const char*,void*,int) = (mode == CPLUS_SAFE_CONSTRAINT_ABORT) ? cplus_sc_abort_handler_s : cplus_sc_ignore_handler_s;
    cplus_sc_set_mem_constraint_handler_s_wrapper(h);
    cplus_sc_set_str_constraint_handler_s_wrapper(h);
    cplus_safe_log(CPLUS_SAFE_LOG_INFO, "safe_error", mode == CPLUS_SAFE_CONSTRAINT_ABORT ? "constraint=abort" : "constraint=ignore");
    cplus_safe_stat_record("set_constraint_mode", 1);
}
cplus_safe_constraint_mode cplus_safe_get_constraint_mode(void) {
    return (cplus_safe_constraint_mode)atomic_load(&g_mode);
}
