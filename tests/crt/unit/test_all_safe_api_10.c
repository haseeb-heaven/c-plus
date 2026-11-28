#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_alloc.h"
#include "safe_io.h"
#include "safe_format.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_platform.h"

#define RUN(f) do{ f(); printf("ok %s\n",#f);}while(0)
static int vsn(char*b,size_t n,const char*f,...){va_list ap;va_start(ap,f);int r=cplus_vsnprintf_s(b,n,f,ap);va_end(ap);return r;}
static int vs(char*b,size_t n,const char*f,...){va_list ap;va_start(ap,f);int r=cplus_vsprintf_s(b,n,f,ap);va_end(ap);return r;}

static void t_memcpy_s(void){ for(int i=0;i<10;i++){ char d[32],s[32]; memset(s,i,32); errno_t r=cplus_memcpy_s(d,32,s,16); assert(r==0); assert(memcmp(d,s,16)==0); assert(cplus_memcpy_s(NULL,32,s,1)!=0); } }
static void t_memmove_s(void){ for(int i=0;i<10;i++){ char b[32]="abcdefghijklmnop"; assert(cplus_memmove_s(b+2,30,b,8)==0); assert(cplus_memmove_s(b,30,NULL,1)!=0);} }
static void t_memset_s(void){ for(int i=0;i<10;i++){ char b[32]; assert(cplus_memset_s(b,32,i,32)==0); assert(b[0]==(char)i); assert(cplus_memset_s(NULL,1,0,1)!=0);} }
static void t_memcmp_s(void){ for(int i=0;i<10;i++){ int diff=1; char a[4]={i,1,2,0},b[4]={i,1,2,0}; assert(cplus_memcmp_s(a,3,b,3,&diff)==0 && diff==0); assert(cplus_memcmp_s(a,3,"zzz",3,&diff)==0); } }
static void t_memzero_s(void){ for(int i=0;i<10;i++){ char b[32]; memset(b,0x7f,32); assert(cplus_memzero_s(b,32)==0); assert(b[0]==0&&b[31]==0); assert(cplus_memzero_s(NULL,1)!=0);} }
static void t_strcpy_s(void){ for(int i=0;i<10;i++){ char d[32],s[16]; snprintf(s,16,"v%d",i); assert(cplus_strcpy_s(d,32,s)==0); assert(strcmp(d,s)==0); assert(cplus_strcpy_s(d,1,"too")!=0);} }
static void t_strncpy_s(void){ for(int i=0;i<10;i++){ char d[32]; assert(cplus_strncpy_s(d,32,"abcdef",3)==0); assert(strcmp(d,"abc")==0); assert(cplus_strncpy_s(d,1,"ab",2)!=0);} }
static void t_strcat_s(void){ for(int i=0;i<10;i++){ char d[32]="a"; assert(cplus_strcat_s(d,32,"b")==0); assert(strcmp(d,"ab")==0); strcpy(d,"123456789012345"); assert(cplus_strcat_s(d,16,"x")!=0);} }
static void t_strncat_s(void){ for(int i=0;i<10;i++){ char d[32]="a"; assert(cplus_strncat_s(d,32,"bcdef",2)==0); assert(strcmp(d,"abc")==0); strcpy(d,"123456789012345"); assert(cplus_strncat_s(d,16,"x",1)!=0);} }
static void t_strcmp_s(void){ for(int i=0;i<10;i++){ int ind=0; assert(cplus_strcmp_s("a",2,"a",&ind)==0 && ind==0); assert(cplus_strcmp_s("a",2,"b",&ind)==0 && ind<0); assert(cplus_strcmp_s(NULL,1,"a",&ind)!=0);} }
static void t_strnlen_s(void){ for(int i=0;i<10;i++){ char s[16]; memset(s,'a',i); s[i]=0; assert(cplus_strnlen_s(s,16)==(rsize_t)i); assert(cplus_strnlen_s(NULL,10)==0);} }
static void t_alloc_s(void){ for(int i=0;i<10;i++){ void*p=malloc_s(16+(size_t)i); assert(p!=NULL); memset(p,0,16+(size_t)i); assert(realloc_s(p,(size_t)-1)==NULL); assert(cplus_safe_ptr_is_live(p)); p=realloc_s(p,64+(size_t)i); assert(p!=NULL); free_s(p); assert(!cplus_safe_ptr_is_live(p)); free_s(p);} }
static void t_calloc_s(void){ for(int i=0;i<10;i++){ void*p=calloc_s(4,4); assert(p!=NULL); for(int j=0;j<16;j++) assert(((unsigned char*)p)[j]==0); free_s(p); errno=0; assert(calloc_s((size_t)-1,2)==NULL && errno==EOVERFLOW);} }
static void t_io_s(void){ for(int i=0;i<10;i++){ char p[64]; snprintf(p,64,"/tmp/cplus_ios_%d.tmp",i); FILE*f=cplus_fopen_s(p,"wb+"); assert(f!=NULL); assert(cplus_fwrite_s("abc",3,1,3,f)==3); rewind(f); char b[8]={0}; assert(cplus_fread_s(b,8,1,3,f)==3); assert(memcmp(b,"abc",3)==0); assert(cplus_fclose_s(f)==0); remove(p);} }
static void t_format_s(void){ for(int i=0;i<10;i++){ char b[32]; assert(cplus_snprintf_s(b,32,"%d",i)>=0); assert(cplus_sprintf_s(b,32,"%s","x")==1); assert(vsn(b,32,"%d",i)>=0); assert(vs(b,32,"%s","y")==1);} }
static void t_stdio_family_s(void){ for(int i=0;i<10;i++){ char p[64]; snprintf(p,64,"/tmp/cplus_fam_%d.tmp",i); FILE*f=cplus_fopen_s(p,"wb+"); assert(f!=NULL); assert(cplus_fprintf_s(f,"%d",i)>=0); rewind(f); int v=-1; assert(cplus_fscanf_s(f,"%d",&v)==1 && v==i); cplus_fclose_s(f); remove(p);} }
static void t_log_platform(void){ for(int i=0;i<10;i++){ cplus_safe_log_set_level((cplus_safe_log_level)(i%6)); (void)cplus_safe_log_get_level(); cplus_safe_log(CPLUS_SAFE_LOG_DEBUG,"test","msg"); cplus_safe_platform_info info; cplus_safe_platform_probe(&info); assert(info.has_fortify==0 || info.has_fortify==1);} }

int main(void){
    RUN(t_memcpy_s); RUN(t_memmove_s); RUN(t_memset_s); RUN(t_memcmp_s); RUN(t_memzero_s);
    RUN(t_strcpy_s); RUN(t_strncpy_s); RUN(t_strcat_s); RUN(t_strncat_s); RUN(t_strcmp_s); RUN(t_strnlen_s);
    RUN(t_alloc_s); RUN(t_calloc_s); RUN(t_io_s); RUN(t_format_s); RUN(t_stdio_family_s); RUN(t_log_platform);
    puts("all_safe_10: ok");
    return 0;
}
