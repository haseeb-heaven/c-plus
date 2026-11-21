#ifndef CPLUS_LIBC_ERRNO_H
#define CPLUS_LIBC_ERRNO_H
#ifdef __cplusplus
extern "C" {
#endif
int *__cplus_errno_location(void);
#define errno (*__cplus_errno_location())
#define ENOMEM 12
#define EINVAL 22
#define ERANGE 34
#define EOVERFLOW 75
#ifdef __cplusplus
}
#endif
#endif
