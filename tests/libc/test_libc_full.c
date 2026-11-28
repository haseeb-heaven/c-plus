#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

void *cplus_malloc(size_t); void *cplus_calloc(size_t,size_t); void *cplus_realloc(void*,size_t); void cplus_free(void*);
void *cplus_memcpy(void*restrict,const void*restrict,size_t); void *cplus_memmove(void*,const void*,size_t);
void *cplus_memset(void*,int,size_t); int cplus_memcmp(const void*,const void*,size_t); void *cplus_memchr(const void*,int,size_t);
size_t cplus_strlen(const char*); size_t cplus_strnlen(const char*,size_t);
char *cplus_strcpy(char*restrict,const char*restrict); char *cplus_strncpy(char*restrict,const char*restrict,size_t);
char *cplus_strcat(char*restrict,const char*restrict); char *cplus_strncat(char*restrict,const char*restrict,size_t);
int cplus_strcmp(const char*,const char*); int cplus_strncmp(const char*,const char*,size_t);
char *cplus_strchr(const char*,int); char *cplus_strrchr(const char*,int); char *cplus_strstr(const char*,const char*);
void *cplus_memrchr(const void*,int,size_t); int cplus_strcasecmp(const char*,const char*); int cplus_strncasecmp(const char*,const char*,size_t); char *cplus_strdup(const char*);
int cplus_isalpha(int); int cplus_isdigit(int); int cplus_isspace(int); int cplus_isalnum(int); int cplus_isupper(int); int cplus_islower(int);
int cplus_isxdigit(int); int cplus_isprint(int); int cplus_isgraph(int); int cplus_ispunct(int); int cplus_iscntrl(int); int cplus_toupper(int); int cplus_tolower(int);
int cplus_abs(int); long cplus_labs(long); int cplus_atoi(const char*); long cplus_atol(const char*);
long cplus_strtol(const char*,char**,int); unsigned long cplus_strtoul(const char*,char**,int);
void cplus_qsort(void*,size_t,size_t,int(*)(const void*,const void*)); void *cplus_bsearch(const void*,const void*,size_t,size_t,int(*)(const void*,const void*));
FILE *cplus_fopen(const char*,const char*); int cplus_fclose(FILE*); size_t cplus_fread(void*,size_t,size_t,FILE*); size_t cplus_fwrite(const void*,size_t,size_t,FILE*);
int cplus_fseek(FILE*,long,int); long cplus_ftell(FILE*); int cplus_fflush(FILE*); int cplus_remove(const char*); int cplus_rename(const char*,const char*);
char *cplus_fgets(char*,int,FILE*); int cplus_fputs(const char*,FILE*); int cplus_getc(FILE*); int cplus_putc(int,FILE*);
int cplus_feof(FILE*); int cplus_ferror(FILE*); void cplus_clearerr(FILE*);
int cplus_printf(const char*,...); int cplus_fprintf(FILE*,const char*,...); int cplus_snprintf(char*,size_t,const char*,...);
void cplus_libc_log_set_enabled(int);

static int icmp(const void*a,const void*b){int x=*(const int*)a,y=*(const int*)b; return (x>y)-(x<y);}

int main(void){
    cplus_libc_log_set_enabled(1); /* exercise logging */
    void *p = cplus_malloc(32); assert(p); cplus_memset(p, 7, 32); assert(((unsigned char*)p)[0]==7);
    void *p2 = cplus_realloc(p, 64); assert(p2); assert(((unsigned char*)p2)[0]==7); cplus_free(p2);
    void *z = cplus_calloc(4,8); assert(z); for(int i=0;i<32;i++) assert(((unsigned char*)z)[i]==0); cplus_free(z);
    char a[8]="abc"; char b[8]; cplus_memcpy(b,a,4); assert(cplus_memcmp(a,b,4)==0);
    cplus_memmove(b+1,b,3); assert(b[1]=='a');
    assert(cplus_memchr("abc",'b',3)==(void*)&"abc"[1]);
    assert(cplus_strlen("abc")==3); assert(cplus_strnlen("abc",2)==2);
    char s[16]; cplus_strcpy(s,"a"); cplus_strcat(s,"b"); cplus_strncat(s,"cde",1); assert(cplus_strcmp(s,"abc")==0);
    cplus_strncpy(s,"xy",8); assert(cplus_strncmp(s,"xy",2)==0);
    assert(cplus_strchr("abc",'b')); assert(cplus_strrchr("abcb",'b')); assert(cplus_strstr("hello","ll"));
    assert(cplus_memrchr("ababa",'a',5));
    assert(cplus_strcasecmp("A","a")==0); assert(cplus_strncasecmp("ab","aX",1)==0);
    char *dup=cplus_strdup("dup"); assert(dup && cplus_strcmp(dup,"dup")==0); free(dup);
    assert(cplus_isalpha('A')&&cplus_isdigit('1')&&cplus_isspace(' ')&&cplus_isalnum('Z')&&cplus_isupper('A')&&cplus_islower('a'));
    assert(cplus_isxdigit('f')&&cplus_isprint('!')&&cplus_isgraph('x')&&cplus_ispunct('!')&&cplus_iscntrl('\n'));
    assert(cplus_toupper('a')=='A' && cplus_tolower('Z')=='z');
    assert(cplus_abs(-1)==1 && cplus_labs(-2L)==2L && cplus_atoi("9")==9 && cplus_atol("10")==10);
    char *end=NULL; assert(cplus_strtol("12z",&end,10)==12); assert(cplus_strtoul("7",&end,10)==7);
    int arr[4]={3,1,4,2}; cplus_qsort(arr,4,sizeof(int),icmp); int key=3; assert(*(int*)cplus_bsearch(&key,arr,4,sizeof(int),icmp)==3);
    const char *path="/tmp/cplus_libc_full.tmp"; const char *path2="/tmp/cplus_libc_full2.tmp";
    FILE *f=cplus_fopen(path,"wb+"); assert(f);
    assert(cplus_fputs("hello",f)>=0); cplus_fprintf(f," %d",1); cplus_fflush(f);
    assert(cplus_fseek(f,0,SEEK_SET)==0); char line[32]={0}; assert(cplus_fgets(line,sizeof(line),f));
    assert(cplus_fseek(f,0,SEEK_END)==0); assert(cplus_ftell(f)>0);
    assert(!cplus_ferror(f)); cplus_clearerr(f); cplus_fclose(f);
    assert(cplus_rename(path,path2)==0); assert(cplus_remove(path2)==0);
    char buf[16]; assert(cplus_snprintf(buf,sizeof(buf),"x%d",2)==2);
    cplus_printf("");
    cplus_libc_log_set_enabled(0);
    puts("libc_full: ok");
    return 0;
}
