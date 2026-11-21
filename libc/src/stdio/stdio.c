#include <stdio.h>
#include <stdarg.h>
#include "../../log/log.h"

FILE *cplus_fopen(const char *path, const char *mode) { cplus_libc_log("fopen","enter"); return fopen(path,mode); }
int cplus_fclose(FILE *stream) { cplus_libc_log("fclose","enter"); return fclose(stream); }
size_t cplus_fread(void *ptr,size_t size,size_t nmemb,FILE*stream){cplus_libc_log("fread","enter"); return fread(ptr,size,nmemb,stream);} 
size_t cplus_fwrite(const void*ptr,size_t size,size_t nmemb,FILE*stream){cplus_libc_log("fwrite","enter"); return fwrite(ptr,size,nmemb,stream);} 
int cplus_vsnprintf(char*str,size_t size,const char*fmt,va_list ap){cplus_libc_log("vsnprintf","enter"); return vsnprintf(str,size,fmt,ap);} 
int cplus_snprintf(char*str,size_t size,const char*fmt,...){cplus_libc_log("snprintf","enter"); va_list ap; va_start(ap,fmt); int r=cplus_vsnprintf(str,size,fmt,ap); va_end(ap); return r;} 
int cplus_printf(const char*fmt,...){cplus_libc_log("printf","enter"); va_list ap; va_start(ap,fmt); int r=vprintf(fmt,ap); va_end(ap); return r;} 
int cplus_fprintf(FILE*stream,const char*fmt,...){cplus_libc_log("fprintf","enter"); va_list ap; va_start(ap,fmt); int r=vfprintf(stream,fmt,ap); va_end(ap); return r;} 
int cplus_scanf(const char*fmt,...){cplus_libc_log("scanf","enter"); va_list ap; va_start(ap,fmt); int r=vscanf(fmt,ap); va_end(ap); return r;} 
int cplus_fscanf(FILE*stream,const char*fmt,...){cplus_libc_log("fscanf","enter"); va_list ap; va_start(ap,fmt); int r=vfscanf(stream,fmt,ap); va_end(ap); return r;} 

int cplus_sprintf(char *str, const char *fmt, ...) { cplus_libc_log("sprintf","enter"); va_list ap; va_start(ap,fmt); int r=vsprintf(str,fmt,ap); va_end(ap); return r; }
int cplus_vsprintf(char *str, const char *fmt, va_list ap){ cplus_libc_log("vsprintf","enter"); return vsprintf(str,fmt,ap); }
int cplus_sscanf(const char *str, const char *fmt, ...) { cplus_libc_log("sscanf","enter"); va_list ap; va_start(ap,fmt); int r=vsscanf(str,fmt,ap); va_end(ap); return r; }
int cplus_vsscanf(const char *str, const char *fmt, va_list ap){ cplus_libc_log("vsscanf","enter"); return vsscanf(str,fmt,ap); }
int cplus_vfprintf(FILE *stream, const char *fmt, va_list ap){ cplus_libc_log("vfprintf","enter"); return vfprintf(stream,fmt,ap); }
int cplus_vprintf(const char *fmt, va_list ap){ cplus_libc_log("vprintf","enter"); return vprintf(fmt,ap); }
int cplus_vfscanf(FILE *stream, const char *fmt, va_list ap){ cplus_libc_log("vfscanf","enter"); return vfscanf(stream,fmt,ap); }
