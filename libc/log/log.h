#ifndef CPLUS_LIBC_LOG_H
#define CPLUS_LIBC_LOG_H
#ifdef __cplusplus
extern "C" {
#endif
void cplus_libc_log(const char *api, const char *msg);
void cplus_libc_log_set_enabled(int enabled);
#ifdef __cplusplus
}
#endif
#endif
