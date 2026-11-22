#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include "../../log/log.h"

#define CPLUS_STDIO_TRACK_CAP 256u
#define CPLUS_STDIO_CLOSED_CAP 256u

static pthread_mutex_t g_stdio_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *g_live_streams[CPLUS_STDIO_TRACK_CAP];
static size_t g_live_stream_count = 0;
static FILE *g_closed_streams[CPLUS_STDIO_CLOSED_CAP];
static size_t g_closed_stream_next = 0;

static int cplus_stdio_is_closed(FILE *stream) {
    for (size_t i = 0; i < CPLUS_STDIO_CLOSED_CAP; ++i) {
        if (g_closed_streams[i] == stream) return 1;
    }
    return 0;
}

static void cplus_stdio_unmark_closed(FILE *stream) {
    for (size_t i = 0; i < CPLUS_STDIO_CLOSED_CAP; ++i) {
        if (g_closed_streams[i] == stream) g_closed_streams[i] = NULL;
    }
}

static void cplus_stdio_mark_closed(FILE *stream) {
    if (!cplus_stdio_is_closed(stream)) {
        g_closed_streams[g_closed_stream_next] = stream;
        g_closed_stream_next = (g_closed_stream_next + 1u) % CPLUS_STDIO_CLOSED_CAP;
    }
}

FILE *cplus_fopen(const char *path, const char *mode) {
    cplus_libc_log("fopen","enter");
    if (!path || !mode) { errno = EINVAL; return NULL; }
    FILE *fp = fopen(path, mode);
    if (fp) {
        pthread_mutex_lock(&g_stdio_lock);
        cplus_stdio_unmark_closed(fp);
        int found = 0;
        for (size_t i = 0; i < g_live_stream_count; ++i) {
            if (g_live_streams[i] == fp) { found = 1; break; }
        }
        if (!found && g_live_stream_count < CPLUS_STDIO_TRACK_CAP) {
            g_live_streams[g_live_stream_count++] = fp;
        }
        pthread_mutex_unlock(&g_stdio_lock);
    }
    return fp;
}

int cplus_fclose(FILE *stream) {
    cplus_libc_log("fclose","enter");
    if (!stream) { errno = EINVAL; return EOF; }
    pthread_mutex_lock(&g_stdio_lock);
    int was_live = 0;
    for (size_t i = 0; i < g_live_stream_count; ++i) {
        if (g_live_streams[i] == stream) {
            g_live_streams[i] = g_live_streams[--g_live_stream_count];
            was_live = 1;
            break;
        }
    }
    if (!was_live && cplus_stdio_is_closed(stream)) {
        pthread_mutex_unlock(&g_stdio_lock);
        errno = EBADF;
        return EOF;
    }
    cplus_stdio_mark_closed(stream);
    pthread_mutex_unlock(&g_stdio_lock);
    return fclose(stream);
}

size_t cplus_fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    cplus_libc_log("fread","enter");
    if (!stream || (size > 0 && nmemb > 0 && !ptr)) { errno = EINVAL; return 0; }
    if (size > 0 && nmemb > SIZE_MAX / size) { errno = EOVERFLOW; return 0; }
    return fread(ptr, size, nmemb, stream);
}

size_t cplus_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    cplus_libc_log("fwrite","enter");
    if (!stream || (size > 0 && nmemb > 0 && !ptr)) { errno = EINVAL; return 0; }
    if (size > 0 && nmemb > SIZE_MAX / size) { errno = EOVERFLOW; return 0; }
    return fwrite(ptr, size, nmemb, stream);
}

int cplus_vsnprintf(char *str, size_t size, const char *fmt, va_list ap) {
    cplus_libc_log("vsnprintf","enter");
    if (!fmt || (size > 0 && !str)) { errno = EINVAL; return -1; }
    return vsnprintf(str, size, fmt, ap);
}

int cplus_snprintf(char *str, size_t size, const char *fmt, ...) {
    cplus_libc_log("snprintf","enter");
    if (!fmt || (size > 0 && !str)) { errno = EINVAL; return -1; }
    va_list ap; va_start(ap, fmt); int r = vsnprintf(str, size, fmt, ap); va_end(ap); return r;
}

int cplus_printf(const char *fmt, ...) {
    cplus_libc_log("printf","enter");
    if (!fmt) { errno = EINVAL; return -1; }
    va_list ap; va_start(ap, fmt); int r = vprintf(fmt, ap); va_end(ap); return r;
}

int cplus_fprintf(FILE *stream, const char *fmt, ...) {
    cplus_libc_log("fprintf","enter");
    if (!stream || !fmt) { errno = EINVAL; return -1; }
    va_list ap; va_start(ap, fmt); int r = vfprintf(stream, fmt, ap); va_end(ap); return r;
}

int cplus_scanf(const char *fmt, ...) {
    cplus_libc_log("scanf","enter");
    if (!fmt) { errno = EINVAL; return EOF; }
    va_list ap; va_start(ap, fmt); int r = vscanf(fmt, ap); va_end(ap); return r;
}

int cplus_fscanf(FILE *stream, const char *fmt, ...) {
    cplus_libc_log("fscanf","enter");
    if (!stream || !fmt) { errno = EINVAL; return EOF; }
    va_list ap; va_start(ap, fmt); int r = vfscanf(stream, fmt, ap); va_end(ap); return r;
}

int cplus_sprintf(char *str, const char *fmt, ...) {
    cplus_libc_log("sprintf","enter");
    if (!str || !fmt) { errno = EINVAL; return -1; }
    va_list ap; va_start(ap, fmt); int r = vsprintf(str, fmt, ap); va_end(ap); return r;
}

int cplus_vsprintf(char *str, const char *fmt, va_list ap) {
    cplus_libc_log("vsprintf","enter");
    if (!str || !fmt) { errno = EINVAL; return -1; }
    return vsprintf(str, fmt, ap);
}

int cplus_sscanf(const char *str, const char *fmt, ...) {
    cplus_libc_log("sscanf","enter");
    if (!str || !fmt) { errno = EINVAL; return EOF; }
    va_list ap; va_start(ap, fmt); int r = vsscanf(str, fmt, ap); va_end(ap); return r;
}

int cplus_vsscanf(const char *str, const char *fmt, va_list ap) {
    cplus_libc_log("vsscanf","enter");
    if (!str || !fmt) { errno = EINVAL; return EOF; }
    return vsscanf(str, fmt, ap);
}

int cplus_vfprintf(FILE *stream, const char *fmt, va_list ap) {
    cplus_libc_log("vfprintf","enter");
    if (!stream || !fmt) { errno = EINVAL; return -1; }
    return vfprintf(stream, fmt, ap);
}

int cplus_vprintf(const char *fmt, va_list ap) {
    cplus_libc_log("vprintf","enter");
    if (!fmt) { errno = EINVAL; return -1; }
    return vprintf(fmt, ap);
}

int cplus_vfscanf(FILE *stream, const char *fmt, va_list ap) {
    cplus_libc_log("vfscanf","enter");
    if (!stream || !fmt) { errno = EINVAL; return EOF; }
    return vfscanf(stream, fmt, ap);
}
