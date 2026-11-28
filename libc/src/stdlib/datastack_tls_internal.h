/* Internal registration hook for the thread-local DataStack cleanup key.
 * Kept out of the public install-include surface: only libc/src callers use it. */
#ifndef CPLUS_DATASTACK_TLS_INTERNAL_H
#define CPLUS_DATASTACK_TLS_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Install the pthread key (once) and mark the calling thread as owning a
 * DataStack, so its backing region is released when the thread exits. */
void cplus_datastack_tls_register_thread(void);

#ifdef __cplusplus
}
#endif
#endif /* CPLUS_DATASTACK_TLS_INTERNAL_H */
