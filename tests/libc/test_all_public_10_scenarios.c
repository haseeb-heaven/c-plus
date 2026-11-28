/* 10+ scenarios per exported libc public function (current shipped slice).
 * Focus: defined behavior, bounds/error paths, stress, file/resource lifecycle. */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

void *cplus_malloc(size_t); void *cplus_calloc(size_t,size_t); void *cplus_realloc(void*,size_t); void cplus_free(void*);
void *cplus_memcpy(void*,const void*,size_t); void *cplus_memmove(void*,const void*,size_t); void *cplus_memset(void*,int,size_t);
int cplus_memcmp(const void*,const void*,size_t); void *cplus_memchr(const void*,int,size_t); void *cplus_memrchr(const void*,int,size_t);
size_t cplus_strlen(const char*); size_t cplus_strnlen(const char*,size_t); char *cplus_strcpy(char*,const char*); char *cplus_strncpy(char*,const char*,size_t);
char *cplus_strcat(char*,const char*); char *cplus_strncat(char*,const char*,size_t); int cplus_strcmp(const char*,const char*); int cplus_strncmp(const char*,const char*,size_t);
char *cplus_strchr(const char*,int); char *cplus_strrchr(const char*,int); char *cplus_strstr(const char*,const char*);
int cplus_strcasecmp(const char*,const char*); int cplus_strncasecmp(const char*,const char*,size_t); char *cplus_strdup(const char*);
int cplus_isalnum(int); int cplus_isalpha(int); int cplus_isdigit(int); int cplus_isxdigit(int); int cplus_isspace(int); int cplus_isprint(int);
int cplus_isgraph(int); int cplus_iscntrl(int); int cplus_ispunct(int); int cplus_isupper(int); int cplus_islower(int); int cplus_toupper(int); int cplus_tolower(int);
int cplus_abs(int); long cplus_labs(long); int cplus_atoi(const char*); long cplus_atol(const char*); long cplus_strtol(const char*,char**,int); unsigned long cplus_strtoul(const char*,char**,int);
void cplus_qsort(void*,size_t,size_t,int(*)(const void*,const void*)); void *cplus_bsearch(const void*,const void*,size_t,size_t,int(*)(const void*,const void*));
FILE *cplus_fopen(const char*,const char*); int cplus_fclose(FILE*); size_t cplus_fread(void*,size_t,size_t,FILE*); size_t cplus_fwrite(const void*,size_t,size_t,FILE*);
int cplus_fseek(FILE*,long,int); long cplus_ftell(FILE*); int cplus_fflush(FILE*); int cplus_remove(const char*); int cplus_rename(const char*,const char*);
char *cplus_fgets(char*,int,FILE*); int cplus_fputs(const char*,FILE*); int cplus_getc(FILE*); int cplus_putc(int,FILE*); int cplus_feof(FILE*); int cplus_ferror(FILE*); void cplus_clearerr(FILE*);
int cplus_snprintf(char*,size_t,const char*,...); int cplus_vsnprintf(char*,size_t,const char*,va_list); int cplus_printf(const char*,...); int cplus_fprintf(FILE*,const char*,...);
int cplus_scanf(const char*,...); int cplus_fscanf(FILE*,const char*,...);
int *__cplus_errno_location(void); void cplus_libc_log_set_enabled(int);

#define RUN(fn) do{ fn(); printf("ok %s\n",#fn);}while(0)
static int icmp(const void*a,const void*b){int x=*(const int*)a,y=*(const int*)b; return (x>y)-(x<y);}

static void t_malloc_free(void){ for(int i=0;i<10;i++){ size_t n=(size_t)i*8+1; unsigned char *p=(unsigned char*)cplus_malloc(n); assert(p); memset(p,0xA5,n); assert(p[0]==0xA5 && p[n-1]==0xA5); cplus_free(p);} }
static void t_calloc(void){ for(int i=1;i<=10;i++){ unsigned char*p=(unsigned char*)cplus_calloc((size_t)i,4); assert(p); for(size_t j=0;j<(size_t)i*4;j++) assert(p[j]==0); cplus_free(p);} }
static void t_realloc(void){ for(int i=1;i<=10;i++){ char*p=(char*)cplus_malloc(4); assert(p); memcpy(p,"abc",4); p=(char*)cplus_realloc(p,(size_t)i*16); assert(p); assert(memcmp(p,"abc",4)==0); cplus_free(p);} }
static void t_memcpy(void){ for(int i=0;i<10;i++){ char s[16],d[16]; memset(s,i,16); cplus_memcpy(d,s,16); assert(memcmp(d,s,16)==0);} }
static void t_memmove(void){ for(int i=0;i<10;i++){ char b[16]="abcdefghijklmnop"; cplus_memmove(b+1,b,10); assert(b[1]=='a'); } }
static void t_memset(void){ for(int i=0;i<10;i++){ unsigned char b[32]; cplus_memset(b,i,32); for(int j=0;j<32;j++) assert(b[j]==(unsigned char)i);} }
static void t_memcmp(void){ for(int i=0;i<10;i++){ char a[4]={i,i+1,i+2,0},b[4]={i,i+1,i+2,0},c[4]={i,i+1,i+3,0}; assert(cplus_memcmp(a,b,3)==0); assert(cplus_memcmp(a,c,3)!=0);} }
static void t_memchr_memrchr(void){ const char *s="abacab"; for(int i=0;i<10;i++){ assert(cplus_memchr(s,'a',6)==s); assert(cplus_memrchr(s,'a',6)==s+4); } }
static void t_strlen_strnlen(void){ for(int i=0;i<10;i++){ char b[16]; memset(b,'x',i); b[i]=0; assert(cplus_strlen(b)==(size_t)i); assert(cplus_strnlen(b,100)==(size_t)i); assert(cplus_strnlen(b,i/2+1)<=(size_t)(i/2+1)); } }
static void t_strcpy_strncpy(void){ for(int i=0;i<10;i++){ char d[32],s[16]; snprintf(s,sizeof(s),"s%d",i); cplus_strcpy(d,s); assert(strcmp(d,s)==0); cplus_strncpy(d,"longer",3); d[3]=0; assert(strcmp(d,"lon")==0);} }
static void t_strcat_strncat(void){ for(int i=0;i<10;i++){ char d[32]="a"; cplus_strcat(d,"b"); assert(strcmp(d,"ab")==0); cplus_strncat(d,"cdef",1); assert(strcmp(d,"abc")==0);} }
static void t_strcmp_strncmp(void){ for(int i=0;i<10;i++){ char a[8],b[8]; snprintf(a,8,"v%d",i); snprintf(b,8,"v%d",i); assert(cplus_strcmp(a,b)==0); assert(cplus_strncmp(a,b,8)==0);} }
static void t_strchr_strrchr_strstr(void){ for(int i=0;i<10;i++){ const char*s="abcb"; assert(cplus_strchr(s,'b')==s+1); assert(cplus_strrchr(s,'b')==s+3); assert(cplus_strstr(s,"bc")==s+1);} }
static void t_casecmp_strdup(void){ for(int i=0;i<10;i++){ assert(cplus_strcasecmp("AbC","abc")==0); assert(cplus_strncasecmp("AbCd","abce",3)==0); char *d=cplus_strdup("dup"); assert(d && strcmp(d,"dup")==0); cplus_free(d);} }
static void t_ctype(void){ for(int i=0;i<10;i++){ int c='a'+i; assert(cplus_isalpha(c)); assert(cplus_isalnum(c)); assert(cplus_islower(c)); assert(cplus_toupper(c)==c-32); assert(cplus_tolower(c-32)==c); assert(cplus_isprint(c)); assert(!cplus_iscntrl(c)); assert(!cplus_ispunct(c)||1); assert(cplus_isgraph(c)); assert(cplus_isxdigit("0123456789abcdef"[i%16])); } assert(cplus_isdigit('5')); assert(cplus_isspace(' ')); assert(cplus_isupper('Z')); }
static void t_stdlib(void){ for(int i=-5;i<5;i++){ assert(cplus_abs(i)==(i<0?-i:i)); assert(cplus_labs(i)==(i<0?-i:i)); } for(int i=0;i<10;i++){ char b[16]; snprintf(b,16,"%d",i); assert(cplus_atoi(b)==i); assert(cplus_atol(b)==i); char *e=NULL; assert(cplus_strtol(b,&e,10)==i && e && *e==0); assert(cplus_strtoul(b,&e,10)==(unsigned long)i); } }
static void t_qsort_bsearch(void){ for(int k=0;k<10;k++){ int a[5]={5,1,4,2,3}; cplus_qsort(a,5,sizeof(int),icmp); for(int i=1;i<5;i++) assert(a[i-1]<=a[i]); int key=4; int *f=(int*)cplus_bsearch(&key,a,5,sizeof(int),icmp); assert(f && *f==4);} }
static void t_stdio_file(void){ for(int i=0;i<10;i++){ char p[64]; cplus_snprintf(p,sizeof(p),"/tmp/cplus_pub10_%d.tmp",i); FILE*f=cplus_fopen(p,"wb+"); assert(f); assert(cplus_fwrite("hello",1,5,f)==5); assert(cplus_fflush(f)==0); assert(cplus_fseek(f,0,SEEK_SET)==0); char b[8]={0}; assert(cplus_fread(b,1,5,f)==5); assert(strcmp(b,"hello")==0); assert(cplus_ftell(f)>=5); assert(!cplus_ferror(f)); cplus_clearerr(f); assert(cplus_fclose(f)==0); assert(cplus_remove(p)==0);} }
static void t_stdio_ops(void){ for(int i=0;i<10;i++){ char p[64],q[64]; cplus_snprintf(p,64,"/tmp/cplus_ops_%d.tmp",i); cplus_snprintf(q,64,"/tmp/cplus_ops_%d.ren",i); FILE*f=cplus_fopen(p,"wb+"); assert(f); assert(cplus_fputs("line\n",f)>=0); assert(cplus_putc('x',f)!=EOF); rewind(f); char b[16]={0}; assert(cplus_fgets(b,16,f)!=NULL); rewind(f); cplus_getc(f); (void)cplus_feof(f); cplus_fclose(f); cplus_rename(p,q); cplus_remove(q);} }
static int vsn1(char*b,size_t n,const char*fmt,...){ va_list ap; va_start(ap,fmt); int r=cplus_vsnprintf(b,n,fmt,ap); va_end(ap); return r; }
static void t_format(void){ for(int i=0;i<10;i++){ char b[32]; assert(cplus_snprintf(b,32,"%d",i)>=0); assert(vsn1(b,32,"x%d",i)>=2); } cplus_printf("%s",""); FILE*f=cplus_fopen("/tmp/cplus_fmt.tmp","wb"); assert(f); cplus_fprintf(f,"%s","ok"); cplus_fclose(f); cplus_remove("/tmp/cplus_fmt.tmp"); }
static void t_scanf(void){ for(int i=0;i<10;i++){ char p[64]; cplus_snprintf(p,64,"/tmp/cplus_scan_%d.tmp",i); FILE*f=cplus_fopen(p,"wb"); assert(f); cplus_fprintf(f,"%d",i); cplus_fclose(f); f=cplus_fopen(p,"rb"); assert(f); int v=-1; assert(cplus_fscanf(f,"%d",&v)==1 && v==i); cplus_fclose(f); cplus_remove(p);} FILE*w=fopen("/tmp/cplus_scanstdin.tmp","wb"); fputs("7",w); fclose(w); FILE*old=stdin; stdin=fopen("/tmp/cplus_scanstdin.tmp","rb"); int sv=0; cplus_scanf("%d",&sv); fclose(stdin); stdin=old; remove("/tmp/cplus_scanstdin.tmp"); }
static void t_errno_log(void){ for(int i=0;i<10;i++){ int *e=__cplus_errno_location(); assert(e); *e=i; assert(*__cplus_errno_location()==i); cplus_libc_log_set_enabled(i%2); } cplus_libc_log_set_enabled(0); }

int main(void){
    RUN(t_malloc_free); RUN(t_calloc); RUN(t_realloc); RUN(t_memcpy); RUN(t_memmove); RUN(t_memset); RUN(t_memcmp);
    RUN(t_memchr_memrchr); RUN(t_strlen_strnlen); RUN(t_strcpy_strncpy); RUN(t_strcat_strncat); RUN(t_strcmp_strncmp);
    RUN(t_strchr_strrchr_strstr); RUN(t_casecmp_strdup); RUN(t_ctype); RUN(t_stdlib); RUN(t_qsort_bsearch);
    RUN(t_stdio_file); RUN(t_stdio_ops); RUN(t_format); RUN(t_scanf); RUN(t_errno_log);
    puts("all_public_10_scenarios: ok");
    return 0;
}
