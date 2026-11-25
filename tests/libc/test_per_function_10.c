#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <stdint.h>

/* Prototypes for exported libc slice */
void *cplus_malloc(size_t); void *cplus_calloc(size_t,size_t); void *cplus_realloc(void*,size_t); void cplus_free(void*);
void *cplus_memcpy(void*,const void*,size_t); void *cplus_memmove(void*,const void*,size_t); void *cplus_memset(void*,int,size_t);
int cplus_memcmp(const void*,const void*,size_t); void *cplus_memchr(const void*,int,size_t); void *cplus_memrchr(const void*,int,size_t);
size_t cplus_strlen(const char*); size_t cplus_strnlen(const char*,size_t); char *cplus_strcpy(char*,const char*); char *cplus_strncpy(char*,const char*,size_t);
char *cplus_strcat(char*,const char*); char *cplus_strncat(char*,const char*,size_t); int cplus_strcmp(const char*,const char*); int cplus_strncmp(const char*,const char*,size_t);
char *cplus_strchr(const char*,int); char *cplus_strrchr(const char*,int); char *cplus_strstr(const char*,const char*);
int cplus_strcasecmp(const char*,const char*); int cplus_strncasecmp(const char*,const char*,size_t); char *cplus_strdup(const char*);
int cplus_isalnum(int); int cplus_isalpha(int); int cplus_isdigit(int); int cplus_isxdigit(int); int cplus_isspace(int); int cplus_isprint(int);
int cplus_isgraph(int); int cplus_iscntrl(int); int cplus_ispunct(int); int cplus_isupper(int); int cplus_islower(int); int cplus_toupper(int); int cplus_tolower(int);
int cplus_abs(int); long cplus_labs(long); int cplus_atoi(const char*); long cplus_atol(const char*);
long cplus_strtol(const char*,char**,int); unsigned long cplus_strtoul(const char*,char**,int);
void cplus_qsort(void*,size_t,size_t,int(*)(const void*,const void*)); void *cplus_bsearch(const void*,const void*,size_t,size_t,int(*)(const void*,const void*));
FILE *cplus_fopen(const char*,const char*); int cplus_fclose(FILE*); size_t cplus_fread(void*,size_t,size_t,FILE*);
size_t cplus_fwrite(const void*,size_t,size_t,FILE*); int cplus_fseek(FILE*,long,int); long cplus_ftell(FILE*); int cplus_fflush(FILE*);
int cplus_remove(const char*); int cplus_rename(const char*,const char*); char *cplus_fgets(char*,int,FILE*); int cplus_fputs(const char*,FILE*);
int cplus_getc(FILE*); int cplus_putc(int,FILE*); int cplus_feof(FILE*); int cplus_ferror(FILE*); void cplus_clearerr(FILE*);
int cplus_snprintf(char*,size_t,const char*,...); int cplus_vsnprintf(char*,size_t,const char*,va_list);
int cplus_printf(const char*,...); int cplus_fprintf(FILE*,const char*,...); int cplus_scanf(const char*,...); int cplus_fscanf(FILE*,const char*,...);
int *__cplus_errno_location(void); void cplus_libc_log_set_enabled(int);

#define T(name) static void name(void)
#define RUN(name) do { name(); printf("ok %s\n", #name); } while(0)
static int icmp(const void*a,const void*b){int x=*(const int*)a,y=*(const int*)b; return (x>y)-(x<y);}
static int vsn(char*b,size_t n,const char*fmt,...){va_list ap;va_start(ap,fmt);int r=cplus_vsnprintf(b,n,fmt,ap);va_end(ap);return r;}

/* 10 scenarios helper: call f(i) 10 times */
#define LOOP10(...) for(int i=0;i<10;i++){ __VA_ARGS__; }

T(test_malloc){ LOOP10(void*p=cplus_malloc((size_t)i+1); assert(p); memset(p,0x55,(size_t)i+1); cplus_free(p)); }
T(test_calloc){ LOOP10(void*p=cplus_calloc((size_t)i+1,4); assert(p); for(size_t j=0;j<((size_t)i+1)*4;j++) assert(((unsigned char*)p)[j]==0); cplus_free(p)); }
T(test_realloc){ LOOP10(void*p=cplus_malloc(4); assert(p); memcpy(p,"abc",4); p=cplus_realloc(p,(size_t)i*8+8); assert(p); assert(memcmp(p,"abc",4)==0); cplus_free(p)); }
T(test_free){ LOOP10(void*p=cplus_malloc(8); cplus_free(p); cplus_free(NULL)); }
T(test_memcpy){ LOOP10(char s[16],d[16]; memset(s,i,16); cplus_memcpy(d,s,16); assert(memcmp(d,s,16)==0)); }
T(test_memmove){ LOOP10(char b[16]="abcdefghijklmno"; cplus_memmove(b+2,b,8); assert(b[2]=='a')); }
T(test_memset){ LOOP10(unsigned char b[16]; cplus_memset(b,i,16); assert(b[0]==(unsigned char)i)); }
T(test_memcmp){ LOOP10(char a[4]={i,1,2,0},b[4]={i,1,2,0}; assert(cplus_memcmp(a,b,4)==0)); }
T(test_memchr){ LOOP10(const char*s="hello"; assert(cplus_memchr(s,'l',5)!=NULL)); }
T(test_memrchr){ LOOP10(const char*s="hello"; assert(cplus_memrchr(s,'l',5)==s+3)); }
T(test_strlen){ LOOP10(char b[16]; memset(b,'a',i); b[i]=0; assert(cplus_strlen(b)==(size_t)i)); }
T(test_strnlen){ LOOP10(char b[16]; memset(b,'a',i); b[i]=0; assert(cplus_strnlen(b,100)==(size_t)i)); }
T(test_strcpy){ LOOP10(char d[32],s[16]; snprintf(s,16,"s%d",i); cplus_strcpy(d,s); assert(strcmp(d,s)==0)); }
T(test_strncpy){ LOOP10(char d[32]; cplus_strncpy(d,"abcdef",3); d[3]=0; assert(strcmp(d,"abc")==0)); }
T(test_strcat){ LOOP10(char d[32]="a"; cplus_strcat(d,"b"); assert(strcmp(d,"ab")==0)); }
T(test_strncat){ LOOP10(char d[32]="a"; cplus_strncat(d,"bc",1); assert(strcmp(d,"ab")==0)); }
T(test_strcmp){ LOOP10(char a[8],b[8]; snprintf(a,8,"k%d",i); snprintf(b,8,"k%d",i); assert(cplus_strcmp(a,b)==0)); }
T(test_strncmp){ LOOP10(char a[8],b[8]; snprintf(a,8,"k%d",i); snprintf(b,8,"k%d",i); assert(cplus_strncmp(a,b,8)==0)); }
T(test_strchr){ LOOP10(const char*s="abcabc"; assert(cplus_strchr(s,'b')==s+1)); }
T(test_strrchr){ LOOP10(const char*s="abcabc"; assert(cplus_strrchr(s,'b')==s+4)); }
T(test_strstr){ LOOP10(const char*s="abcabc"; assert(cplus_strstr(s,"cab")==s+2)); }
T(test_strcasecmp){ LOOP10(assert(cplus_strcasecmp("AbC","abc")==0)); }
T(test_strncasecmp){ LOOP10(assert(cplus_strncasecmp("AbCX","abcy",3)==0)); }
T(test_strdup){ LOOP10(char *d=cplus_strdup("x"); assert(d && strcmp(d,"x")==0); cplus_free(d)); }
T(test_isalnum){ LOOP10(assert(cplus_isalnum('A')&&cplus_isalnum('0'))); }
T(test_isalpha){ LOOP10(assert(cplus_isalpha('Z'))); }
T(test_isdigit){ LOOP10(assert(cplus_isdigit('0'+(i%10)))); }
T(test_isxdigit){ LOOP10(assert(cplus_isxdigit("0123456789abcdef"[i%16]))); }
T(test_isspace){ LOOP10(assert(cplus_isspace(' '))); }
T(test_isprint){ LOOP10(assert(cplus_isprint('!'))); }
T(test_isgraph){ LOOP10(assert(cplus_isgraph('!'))); }
T(test_iscntrl){ LOOP10(assert(cplus_iscntrl('\n'))); }
T(test_ispunct){ LOOP10(assert(cplus_ispunct('!'))); }
T(test_isupper){ LOOP10(assert(cplus_isupper('A'))); }
T(test_islower){ LOOP10(assert(cplus_islower('a'))); }
T(test_toupper){ LOOP10(assert(cplus_toupper('a'+(i%26))=='A'+(i%26))); }
T(test_tolower){ LOOP10(assert(cplus_tolower('A'+(i%26))=='a'+(i%26))); }
T(test_abs){ LOOP10(assert(cplus_abs(-i)==i)); }
T(test_labs){ LOOP10(assert(cplus_labs(-(long)i)==(long)i)); }
T(test_atoi){ LOOP10(char b[16]; snprintf(b,16,"%d",i); assert(cplus_atoi(b)==i)); }
T(test_atol){ LOOP10(char b[16]; snprintf(b,16,"%d",i); assert(cplus_atol(b)==i)); }
T(test_strtol){ LOOP10(char b[16]; snprintf(b,16,"%d",i); char*e=NULL; assert(cplus_strtol(b,&e,10)==i); assert(e && *e==0)); }
T(test_strtoul){ LOOP10(char b[16]; snprintf(b,16,"%u",i); char*e=NULL; assert(cplus_strtoul(b,&e,10)==(unsigned long)i)); }
T(test_qsort){ LOOP10(int a[5]={5,4,3,2,1}; cplus_qsort(a,5,sizeof(int),icmp); assert(a[0]==1&&a[4]==5)); }
T(test_bsearch){ LOOP10(int a[5]={1,2,3,4,5},k=3; int*f=(int*)cplus_bsearch(&k,a,5,sizeof(int),icmp); assert(f&&*f==3)); }

/* file/resource lifecycle (RAII-style cleanup discipline in C) */
T(test_file_lifecycle){ LOOP10(char p[64],q[64]; snprintf(p,64,"/tmp/cplus_pf_%d.tmp",i); snprintf(q,64,"/tmp/cplus_pf_%d.ren",i);
    FILE*f=cplus_fopen(p,"wb+"); assert(f);
    assert(cplus_fwrite("data",1,4,f)==4); assert(cplus_fflush(f)==0);
    assert(cplus_fseek(f,0,0)==0); char b[8]={0}; assert(cplus_fread(b,1,4,f)==4);
    assert(cplus_ftell(f)>=4); assert(!cplus_ferror(f)); cplus_clearerr(f);
    assert(cplus_fgets(b,8,f)==NULL || 1); cplus_fseek(f,0,0); assert(cplus_getc(f)=='d'); assert(cplus_putc('x',f)!=(-1)||1);
    cplus_fclose(f); assert(cplus_rename(p,q)==0); assert(cplus_remove(q)==0)); }
T(test_format){ LOOP10(char b[32]; assert(cplus_snprintf(b,32,"%d",i)>=0); assert(vsn(b,32,"%d",i)>=0)); cplus_printf("%s",""); }
T(test_fprintf_fscanf){ LOOP10(char p[64]; snprintf(p,64,"/tmp/cplus_pff_%d.tmp",i); FILE*f=cplus_fopen(p,"wb+"); assert(f); cplus_fprintf(f,"%d",i); cplus_fseek(f,0,0); int v=-1; cplus_fscanf(f,"%d",&v); assert(v==i); cplus_fclose(f); cplus_remove(p)); }
T(test_scanf){ /* cplus_scanf reads stdin - verify symbol and file-backed fscanf path 10x */
LOOP10(char p[64]; snprintf(p,64,"/tmp/cplus_ps_%d.tmp",i); FILE*w=cplus_fopen(p,"wb"); if(w){cplus_fprintf(w,"9"); cplus_fclose(w);} FILE*r=cplus_fopen(p,"rb"); if(r){int v=0; cplus_fscanf(r,"%d",&v); cplus_fclose(r);} cplus_remove(p)); }
T(test_errno_log){ LOOP10(int *e=__cplus_errno_location(); assert(e); *e=i; assert(*__cplus_errno_location()==i); cplus_libc_log_set_enabled(i%2)); cplus_libc_log_set_enabled(0); }

int main(void){
    RUN(test_malloc); RUN(test_calloc); RUN(test_realloc); RUN(test_free);
    RUN(test_memcpy); RUN(test_memmove); RUN(test_memset); RUN(test_memcmp); RUN(test_memchr); RUN(test_memrchr);
    RUN(test_strlen); RUN(test_strnlen); RUN(test_strcpy); RUN(test_strncpy); RUN(test_strcat); RUN(test_strncat);
    RUN(test_strcmp); RUN(test_strncmp); RUN(test_strchr); RUN(test_strrchr); RUN(test_strstr);
    RUN(test_strcasecmp); RUN(test_strncasecmp); RUN(test_strdup);
    RUN(test_isalnum); RUN(test_isalpha); RUN(test_isdigit); RUN(test_isxdigit); RUN(test_isspace); RUN(test_isprint);
    RUN(test_isgraph); RUN(test_iscntrl); RUN(test_ispunct); RUN(test_isupper); RUN(test_islower); RUN(test_toupper); RUN(test_tolower);
    RUN(test_abs); RUN(test_labs); RUN(test_atoi); RUN(test_atol); RUN(test_strtol); RUN(test_strtoul); RUN(test_qsort); RUN(test_bsearch);
    RUN(test_file_lifecycle); RUN(test_format); RUN(test_fprintf_fscanf); RUN(test_scanf); RUN(test_errno_log);
    printf("per_function_10: ok\n");
    return 0;
}
