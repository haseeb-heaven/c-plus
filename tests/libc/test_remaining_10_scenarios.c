#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

void *cplus_memchr(const void*,int,size_t); void *cplus_memrchr(const void*,int,size_t);
int cplus_strcasecmp(const char*,const char*); int cplus_strncasecmp(const char*,const char*,size_t);
char *cplus_strdup(const char*); char *cplus_strchr(const char*,int); char *cplus_strrchr(const char*,int);
char *cplus_strstr(const char*,const char*); char *cplus_strncat(char*,const char*,size_t);
int cplus_tolower(int); int cplus_toupper(int);
int cplus_abs(int); long cplus_labs(long); int cplus_atoi(const char*); long cplus_atol(const char*);
long cplus_strtol(const char*,char**,int); unsigned long cplus_strtoul(const char*,char**,int);
void cplus_qsort(void*,size_t,size_t,int(*)(const void*,const void*)); void *cplus_bsearch(const void*,const void*,size_t,size_t,int(*)(const void*,const void*));
char *cplus_fgets(char*,int,FILE*); int cplus_fputs(const char*,FILE*); int cplus_getc(FILE*); int cplus_putc(int,FILE*);
int cplus_feof(FILE*); int cplus_ferror(FILE*); void cplus_clearerr(FILE*); int cplus_fflush(FILE*); int cplus_fseek(FILE*,long,int); long cplus_ftell(FILE*);
int cplus_rename(const char*,const char*); int cplus_remove(const char*); FILE *cplus_fopen(const char*,const char*); int cplus_fclose(FILE*);
size_t cplus_fread(void*,size_t,size_t,FILE*); size_t cplus_fwrite(const void*,size_t,size_t,FILE*);
int cplus_snprintf(char*,size_t,const char*,...); int cplus_vsnprintf(char*,size_t,const char*,va_list);
int cplus_fprintf(FILE*,const char*,...); int cplus_fscanf(FILE*,const char*,...); int cplus_printf(const char*,...); int cplus_scanf(const char*,...);
void cplus_free(void*);

#define RUN(fn) do{ fn(); printf("ok %s\n",#fn);}while(0)
static int icmp(const void*a,const void*b){int x=*(const int*)a,y=*(const int*)b; return (x>y)-(x<y);}
static int vsn(char*b,size_t n,const char*fmt,...){va_list ap;va_start(ap,fmt);int r=cplus_vsnprintf(b,n,fmt,ap);va_end(ap);return r;}

static void t_memchr(void){ const char *s="abcabc"; for(int i=0;i<10;i++){ assert(cplus_memchr(s,'b',6)==s+1); assert(cplus_memchr(s,'z',6)==NULL); } }
static void t_memrchr(void){ const char *s="abcabc"; for(int i=0;i<10;i++){ assert(cplus_memrchr(s,'b',6)==s+4); assert(cplus_memrchr(s,'z',6)==NULL); } }
static void t_strcase(void){ for(int i=0;i<10;i++){ assert(cplus_strcasecmp("Hello","hello")==0); assert(cplus_strncasecmp("HelloX","helloY",5)==0); assert(cplus_strcasecmp("a","b")<0); } }
static void t_strdup(void){ for(int i=0;i<10;i++){ char s[16]; snprintf(s,16,"d%d",i); char *d=cplus_strdup(s); assert(d && strcmp(d,s)==0); cplus_free(d);} }
static void t_strchr_rchr_strstr(void){ for(int i=0;i<10;i++){ const char *s="abcabc"; assert(cplus_strchr(s,'c')==s+2); assert(cplus_strrchr(s,'c')==s+5); assert(cplus_strstr(s,"cab")==s+2); assert(cplus_strstr(s,"zzz")==NULL);} }
static void t_strncat_extra(void){ for(int i=0;i<10;i++){ char d[32]="ab"; cplus_strncat(d,"cdef",2); assert(strcmp(d,"abcd")==0);} }
static void t_ctype_extra(void){ for(int i=0;i<10;i++){ assert(cplus_tolower('A'+i)=='a'+i); assert(cplus_toupper('a'+i)=='A'+i); } }
static void t_stdlib_extra(void){ for(int i=-5;i<5;i++){ assert(cplus_abs(i)==(i<0?-i:i)); assert(cplus_labs((long)i)==(i<0?-i:i)); } for(int i=0;i<10;i++){ char b[16]; snprintf(b,16,"%d",i*10); assert(cplus_atoi(b)==i*10); assert(cplus_atol(b)==i*10); char *e=NULL; assert(cplus_strtol(b,&e,10)==i*10); assert(cplus_strtoul(b,&e,10)==(unsigned long)(i*10)); } }
static void t_qsort_bsearch_extra(void){ for(int i=0;i<10;i++){ int a[6]={9,1,5,3,7,2}; cplus_qsort(a,6,sizeof(int),icmp); for(int j=1;j<6;j++) assert(a[j-1]<=a[j]); int k=5; int*f=(int*)cplus_bsearch(&k,a,6,sizeof(int),icmp); assert(f&&*f==5);} }
static void t_stdio2(void){ for(int i=0;i<10;i++){
    char p[64],q[64]; cplus_snprintf(p,64,"/tmp/cplus_r10_%d.tmp",i); cplus_snprintf(q,64,"/tmp/cplus_r10_%d.ren",i);
    FILE*f=cplus_fopen(p,"wb+"); assert(f);
    assert(cplus_fputs("hello\n",f)>=0); assert(cplus_putc('!',f)!=EOF); assert(cplus_fflush(f)==0);
    assert(cplus_fseek(f,0,SEEK_SET)==0);
    char b[32]={0}; assert(cplus_fgets(b,sizeof(b),f)!=NULL);
    rewind(f); int c=cplus_getc(f); assert(c=='h');
    assert(!cplus_ferror(f)); cplus_clearerr(f); (void)cplus_feof(f);
    assert(cplus_ftell(f)>=1);
    cplus_fclose(f);
    assert(cplus_rename(p,q)==0); assert(cplus_remove(q)==0);
}}
static void t_format_extra(void){ for(int i=0;i<10;i++){ char b[32]; assert(cplus_snprintf(b,32,"%d",i)>=0); assert(vsn(b,32,"%s", "x")>=1); } FILE*f=cplus_fopen("/tmp/cplus_fmt2.tmp","wb"); assert(f); cplus_fprintf(f,"%d",1); cplus_fclose(f); f=cplus_fopen("/tmp/cplus_fmt2.tmp","rb"); int v=0; cplus_fscanf(f,"%d",&v); cplus_fclose(f); cplus_remove("/tmp/cplus_fmt2.tmp"); cplus_printf("%s",""); }
static void t_scanf_extra(void){ for(int i=0;i<10;i++){ char p[64]; cplus_snprintf(p,64,"/tmp/cplus_s10_%d.tmp",i); FILE*f=cplus_fopen(p,"wb"); cplus_fprintf(f,"%d",i+100); cplus_fclose(f); f=cplus_fopen(p,"rb"); int v=0; assert(cplus_fscanf(f,"%d",&v)==1 && v==i+100); cplus_fclose(f); cplus_remove(p);} }

int main(void){
    RUN(t_memchr); RUN(t_memrchr); RUN(t_strcase); RUN(t_strdup); RUN(t_strchr_rchr_strstr);
    RUN(t_strncat_extra); RUN(t_ctype_extra); RUN(t_stdlib_extra); RUN(t_qsort_bsearch_extra);
    RUN(t_stdio2); RUN(t_format_extra); RUN(t_scanf_extra);
    puts("remaining_10_scenarios: ok");
    return 0;
}
