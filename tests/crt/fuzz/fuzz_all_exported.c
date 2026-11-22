/* Fuzz/stress for exported safe+libc slice. Randomized but deterministic seed.
 * Goal: no crashes, defined behavior for checked APIs, resource cleanup. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_alloc.h"
#include "safe_io.h"
#include "safe_format.h"
#include "safe_log.h"
#include "safe_platform.h"

/* libc prototypes */
void *cplus_malloc(size_t); void *cplus_calloc(size_t,size_t); void *cplus_realloc(void*,size_t); void cplus_free(void*);
void *cplus_memcpy(void*,const void*,size_t); void *cplus_memmove(void*,const void*,size_t); void *cplus_memset(void*,int,size_t);
int cplus_memcmp(const void*,const void*,size_t); void *cplus_memchr(const void*,int,size_t); void *cplus_memrchr(const void*,int,size_t);
size_t cplus_strlen(const char*); size_t cplus_strnlen(const char*,size_t); char *cplus_strcpy(char*,const char*); char *cplus_strncpy(char*,const char*,size_t);
char *cplus_strcat(char*,const char*); char *cplus_strncat(char*,const char*,size_t); int cplus_strcmp(const char*,const char*); int cplus_strncmp(const char*,const char*,size_t);
char *cplus_strchr(const char*,int); char *cplus_strrchr(const char*,int); char *cplus_strstr(const char*,const char*);
int cplus_strcasecmp(const char*,const char*); int cplus_strncasecmp(const char*,const char*,size_t); char *cplus_strdup(const char*);
int cplus_tolower(int);
FILE *cplus_fopen(const char*,const char*); int cplus_fclose(FILE*); size_t cplus_fread(void*,size_t,size_t,FILE*);
size_t cplus_fwrite(const void*,size_t,size_t,FILE*); int cplus_fflush(FILE*); int cplus_fseek(FILE*,long,int); long cplus_ftell(FILE*); int cplus_remove(const char*); int cplus_rename(const char*,const char*);
char *cplus_fgets(char*,int,FILE*); int cplus_fputs(const char*,FILE*); int cplus_getc(FILE*); int cplus_putc(int,FILE*);
int cplus_feof(FILE*); int cplus_ferror(FILE*); void cplus_clearerr(FILE*);
int cplus_snprintf(char*,size_t,const char*,...); int cplus_vsnprintf(char*,size_t,const char*,va_list);
int cplus_printf(const char*,...); int cplus_fprintf(FILE*,const char*,...); int cplus_fscanf(FILE*,const char*,...);

static unsigned st=0xC0FFEEu;
static unsigned R(void){ st=st*1664525u+1013904223u; return st; }
static int vsn_wrap(char*b,size_t n,const char*f,...){va_list ap;va_start(ap,f);int r=cplus_vsnprintf(b,n,f,ap);va_end(ap);return r;}
static int vsn_s_wrap(char*b,size_t n,const char*f,...){va_list ap;va_start(ap,f);int r=cplus_vsnprintf_s(b,n,f,ap);va_end(ap);return r;}
static int vs_s_wrap(char*b,size_t n,const char*f,...){va_list ap;va_start(ap,f);int r=cplus_vsprintf_s(b,n,f,ap);va_end(ap);return r;}

int main(void){
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_ERROR);
    for(int i=0;i<1500;i++){
        unsigned char sb[64], db[64]; for(int j=0;j<64;j++) sb[j]=(unsigned char)R();
        size_t n=R()%65;
        (void)cplus_memcpy(db,sb,n); (void)cplus_memmove(db,db,(n? n-1:0)); (void)cplus_memset(db,(int)R(),n);
        (void)cplus_memcmp(db,sb,n); (void)cplus_memchr(sb,'a',64); (void)cplus_memrchr(sb,'a',64);
        (void)cplus_memcpy_s(db,64,sb,n); (void)cplus_memmove_s(db,64,sb,n); (void)cplus_memset_s(db,64,(int)R()&255,n); int diff=0; (void)cplus_memcmp_s(db,64,sb,n,&diff); (void)cplus_memzero_s(db,64);

        char ss[32]; for(int j=0;j<31;j++) ss[j]=(char)('a'+(R()%26)); ss[R()%31]=0; char sd[32];
        (void)cplus_strlen(ss); (void)cplus_strnlen(ss,R()%40); cplus_strcpy(sd,ss); cplus_strncpy(sd,ss,R()%32);
        strcpy(sd,"a"); cplus_strcat(sd,"b"); strcpy(sd,"a"); cplus_strncat(sd,ss,R()%20);
        (void)cplus_strcmp(sd,ss); (void)cplus_strncmp(sd,ss,R()%10);
        (void)cplus_strchr(ss,'a'); (void)cplus_strrchr(ss,'a'); (void)cplus_strstr(ss,"ab");
        (void)cplus_strcasecmp(ss,"ABC"); (void)cplus_strncasecmp(ss,"ABC",2); char*dup=cplus_strdup(ss); cplus_free(dup);
        (void)cplus_strcpy_s(sd,32,ss); (void)cplus_strncpy_s(sd,32,ss,R()%40); strcpy(sd,"a"); (void)cplus_strcat_s(sd,32,ss); strcpy(sd,"a"); (void)cplus_strncat_s(sd,32,ss,R()%40);
        int ind=0; (void)cplus_strcmp_s(sd,32,ss,&ind); (void)cplus_strnlen_s(ss,R()%40);

        void *p=cplus_malloc(R()%512); if(p){ p=cplus_realloc(p,R()%1024); } cplus_free(p);
        void *c=cplus_calloc(R()%16+1,R()%16+1); cplus_free(c);
        void *sp=malloc_s(R()%512); if(sp){ void *np=realloc_s(sp,R()%512); if(np) sp=np; free_s(sp);} 
        void *sc=calloc_s(R()%16+1,R()%16+1); if(sc) free_s(sc);

        char path[64], path2[64]; snprintf(path,64,"/tmp/cplus_fx_%d.tmp",i%16); snprintf(path2,64,"/tmp/cplus_fx_%d.ren",i%16);
        FILE*f=cplus_fopen(path,"wb+");
        if(f){ cplus_fwrite("abc",1,3,f); cplus_fflush(f); cplus_fseek(f,0,0); char rb[8]={0}; cplus_fread(rb,1,3,f); cplus_fgets(rb,8,f); cplus_putc('x',f); cplus_fseek(f,0,0); cplus_getc(f); cplus_clearerr(f); (void)cplus_feof(f); (void)cplus_ferror(f); cplus_fclose(f); cplus_rename(path,path2); cplus_remove(path2); }
        FILE*sf=cplus_fopen_s(path,"wb+"); if(sf){ cplus_fwrite_s("abc",3,1,3,sf); cplus_fseek(sf,0,0); char rb[8]={0}; cplus_fread_s(rb,8,1,3,sf); cplus_fprintf_s(sf,"%d",i); cplus_fseek(sf,0,0); int v=0; cplus_fscanf_s(sf,"%d",&v); cplus_fclose_s(sf); cplus_remove(path); }

        char fb[64]; cplus_snprintf(fb,sizeof(fb),"%d",i); vsn_wrap(fb,sizeof(fb),"%d",i); cplus_sprintf_s(fb,sizeof(fb),"%d",i); vsn_s_wrap(fb,sizeof(fb),"%d",i); vs_s_wrap(fb,sizeof(fb),"%d",i);
        cplus_printf("%s",""); cplus_printf_s("%s","");
        cplus_safe_platform_info info; cplus_safe_platform_probe(&info); cplus_safe_log(CPLUS_SAFE_LOG_DEBUG,"fuzz","tick"); (void)cplus_tolower('A');
    }
    printf("fuzz_all_exported: ok\n");
    return 0;
}
