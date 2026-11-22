/* Platform-backed stdio position/stream extras (C+ libc slice), batch 3.
 * Delegate to the installed system libc; logged per C+ convention.
 * POSIX feature macros expose fileno/fdopen/freopen/fgetpos/fsetpos. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "../../log/log.h"
#include "stdio_internal.h"
#include <errno.h>
#include <stdio.h>

int cplus_setvbuf(FILE *stream,char *buf,int mode,size_t size){ cplus_libc_log("setvbuf","enter"); return setvbuf(stream,buf,mode,size); }
void cplus_setbuf(FILE *stream,char *buf){ cplus_libc_log("setbuf","enter"); setbuf(stream,buf); }
int cplus_fgetpos(FILE *stream,fpos_t *pos){ cplus_libc_log("fgetpos","enter"); return fgetpos(stream,pos); }
int cplus_fsetpos(FILE *stream,const fpos_t *pos){ cplus_libc_log("fsetpos","enter"); return fsetpos(stream,pos); }
int cplus_ungetc(int c,FILE *stream){ cplus_libc_log("ungetc","enter"); return ungetc(c,stream); }
int cplus_fileno(FILE *stream){ cplus_libc_log("fileno","enter"); return fileno(stream); }
FILE *cplus_fdopen(int fd,const char *mode){
    cplus_libc_log("fdopen","enter");
    FILE *fp = fdopen(fd,mode);
    if (fp) cplus_stdio_register(fp);
    return fp;
}
FILE *cplus_freopen(const char *path,const char *mode,FILE *stream){
    cplus_libc_log("freopen","enter");
    if (!path || !mode) { errno = EINVAL; return NULL; }
    FILE *fp = freopen(path,mode,stream);
    if (fp) cplus_stdio_register(fp);
    return fp;
}
FILE *cplus_tmpfile(void){
    cplus_libc_log("tmpfile","enter");
    FILE *fp = tmpfile();
    /* Without this registration the double-close guard can retain a stale
     * "closed" marker for a recycled FILE* address and cplus_fclose() then
     * refuses to close the new stream, leaking the descriptor. */
    if (fp) cplus_stdio_register(fp);
    return fp;
}
void cplus_rewind(FILE *stream){
    cplus_libc_log("rewind","enter");
    if (!stream) { errno = EINVAL; return; }
    rewind(stream);
}
int cplus_fgetc(FILE *stream){
    cplus_libc_log("fgetc","enter");
    if (!stream) { errno = EINVAL; return EOF; }
    return fgetc(stream);
}
int cplus_fputc(int c, FILE *stream){
    cplus_libc_log("fputc","enter");
    if (!stream) { errno = EINVAL; return EOF; }
    return fputc(c, stream);
}
