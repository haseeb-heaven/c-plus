/* Platform-backed stdio position/stream extras (C+ libc slice), batch 3.
 * Delegate to the installed system libc; logged per C+ convention.
 * POSIX feature macros expose fileno/fdopen/freopen/fgetpos/fsetpos. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "../../log/log.h"
#include <stdio.h>

int cplus_setvbuf(FILE *stream,char *buf,int mode,size_t size){ cplus_libc_log("setvbuf","enter"); return setvbuf(stream,buf,mode,size); }
void cplus_setbuf(FILE *stream,char *buf){ cplus_libc_log("setbuf","enter"); setbuf(stream,buf); }
int cplus_fgetpos(FILE *stream,fpos_t *pos){ cplus_libc_log("fgetpos","enter"); return fgetpos(stream,pos); }
int cplus_fsetpos(FILE *stream,const fpos_t *pos){ cplus_libc_log("fsetpos","enter"); return fsetpos(stream,pos); }
int cplus_ungetc(int c,FILE *stream){ cplus_libc_log("ungetc","enter"); return ungetc(c,stream); }
int cplus_fileno(FILE *stream){ cplus_libc_log("fileno","enter"); return fileno(stream); }
FILE *cplus_fdopen(int fd,const char *mode){ cplus_libc_log("fdopen","enter"); return fdopen(fd,mode); }
FILE *cplus_freopen(const char *path,const char *mode,FILE *stream){ cplus_libc_log("freopen","enter"); return freopen(path,mode,stream); }
FILE *cplus_tmpfile(void){ cplus_libc_log("tmpfile","enter"); return tmpfile(); }
