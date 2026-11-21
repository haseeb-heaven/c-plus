#include <stdio.h>
#include "../../log/log.h"
int cplus_fseek(FILE *s,long off,int whence){ cplus_libc_log("fseek","enter"); return fseek(s,off,whence); }
long cplus_ftell(FILE *s){ cplus_libc_log("ftell","enter"); return ftell(s); }
int cplus_fflush(FILE *s){ cplus_libc_log("fflush","enter"); return fflush(s); }
int cplus_remove(const char *path){ cplus_libc_log("remove","enter"); return remove(path); }
int cplus_rename(const char *oldp,const char *newp){ cplus_libc_log("rename","enter"); return rename(oldp,newp); }
char *cplus_fgets(char *s,int n,FILE *stream){ cplus_libc_log("fgets","enter"); return fgets(s,n,stream); }
int cplus_fputs(const char *s,FILE *stream){ cplus_libc_log("fputs","enter"); return fputs(s,stream); }
int cplus_getc(FILE *s){ cplus_libc_log("getc","enter"); return getc(s); }
int cplus_putc(int c,FILE *s){ cplus_libc_log("putc","enter"); return putc(c,s); }
int cplus_feof(FILE *s){ cplus_libc_log("feof","enter"); return feof(s); }
int cplus_ferror(FILE *s){ cplus_libc_log("ferror","enter"); return ferror(s); }
void cplus_clearerr(FILE *s){ cplus_libc_log("clearerr","enter"); clearerr(s); }
