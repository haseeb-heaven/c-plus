/* Rust-like runtime behaviour for each hardened ordinary-C method.
 *
 * Every scenario runs in a forked child so one failure cannot hide the rest.
 * The parent classifies the child as ACCEPTED (exit 0), REFUSED (exit 2 with
 * an errno-based refusal) or CRASH (signal / other exit).
 *
 * Default: C+ SafeCRT layer. -DPLAIN_LIBC: plain libc layer.
 */
#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#else
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"
#endif
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define REFUSED 2
#define ACCEPTED 0

static void outcome(int code) { _exit(code); }

/* ---- allocation methods ------------------------------------------------- */
static void malloc_valid(void) {
    void *p = malloc(64); if (!p) outcome(3); memset(p, 1, 64); free(p); outcome(ACCEPTED);
}
static void malloc_zero(void) {
    void *p = malloc(0); if (!p) outcome(3); free(p); outcome(ACCEPTED);
}
static void malloc_huge(void) {
    void *p = malloc((size_t)-1); if (!p) outcome(REFUSED); free(p); outcome(ACCEPTED);
}
static void malloc_reuse(void) {
    for (int i = 0; i < 100; ++i) { void *p = malloc(32); if (!p) outcome(3); free(p); }
    outcome(ACCEPTED);
}

static void calloc_valid(void) {
    void *p = calloc(8, 8); if (!p) outcome(3); free(p); outcome(ACCEPTED);
}
static void calloc_zero(void) {
    void *p = calloc(0, 8); if (!p) outcome(3); free(p); outcome(ACCEPTED);
}
static void calloc_overflow(void) {
    void *p = calloc((size_t)-1, 8); if (!p) outcome(REFUSED); free(p); outcome(3);
}
static void calloc_huge(void) {
    void *p = calloc(1024, 1024); if (!p) outcome(3); free(p); outcome(ACCEPTED);
}

static void realloc_grow(void) {
    char *p = malloc(16); if (!p) outcome(3); memset(p, 'a', 16);
    char *q = realloc(p, 256); if (!q) outcome(3); memset(q, 'b', 256); free(q); outcome(ACCEPTED);
}
static void realloc_shrink(void) {
    char *p = malloc(256); if (!p) outcome(3); char *q = realloc(p, 16);
    if (!q) outcome(3); memset(q, 'c', 16); free(q); outcome(ACCEPTED);
}
static void realloc_huge_preserve(void) {
    char *p = malloc(32); if (!p) outcome(3); memset(p, 'z', 32);
    char *q = realloc(p, (size_t)-1); if (!q) { memset(p, 'z', 32); free(p); outcome(REFUSED); }
    free(q); outcome(3);
}
static void realloc_invalid(void) {
    char stack[8]; char *q = realloc(stack, 32); if (q) { free(q); outcome(3); } outcome(REFUSED);
}

static void free_valid(void) { void *p = malloc(16); if (!p) outcome(3); free(p); outcome(ACCEPTED); }
static void free_null(void) { free(NULL); outcome(ACCEPTED); }
static void free_double_alias(void) {
    void *a = malloc(64); if (!a) outcome(3); void *b = a; free(a);
    errno = 0; free(b); outcome(errno ? REFUSED : 3);
}
static void free_foreign(void) {
    char stack[8]; errno = 0; free(stack); outcome(errno ? REFUSED : 3);
}

/* ---- memory methods ----------------------------------------------------- */
static void memset_exact(void) { char *p = malloc(16); if (!p) outcome(3); memset(p, 1, 16); free(p); outcome(ACCEPTED); }
static void memset_zero(void) { char *p = malloc(16); if (!p) outcome(3); memset(p, 1, 0); free(p); outcome(ACCEPTED); }
static void memset_over(void) { char *p = malloc(16); if (!p) outcome(3); errno=0; memset(p, 1, 32); free(p); outcome(errno ? REFUSED : 3); }
static void memset_end(void) { char *p = malloc(16); if (!p) outcome(3); errno=0; memset(p+16, 1, 1); free(p); outcome(errno ? REFUSED : 3); }
static void memset_null(void) { errno=0; memset(NULL, 1, 4); outcome(errno ? REFUSED : 3); }

static void memcpy_exact(void) { char a[16],b[16]; memset(a,1,16); memcpy(b,a,16); outcome(memcmp(a,b,16)?3:ACCEPTED); }
static void memcpy_zero(void) { char a[4],b[4]; memcpy(b,a,0); outcome(ACCEPTED); }
static void memcpy_over(void) { char *p=malloc(16); char src[32]; memset(src,2,32); if(!p)outcome(3); errno=0; memcpy(p,src,32); free(p); outcome(errno?REFUSED:3); }
static void memcpy_overlap(void) { char *p=malloc(32); if(!p)outcome(3); memcpy(p,"abcdefghijklmnop",16); errno=0; memcpy(p+4,p,12); free(p); outcome(errno?REFUSED:3); }
static void memcpy_null(void) { char a[4]; errno=0; memcpy(a,NULL,4); outcome(errno?REFUSED:3); }

/* ---- string methods ----------------------------------------------------- */
static void strcpy_exact(void) { char d[8]; errno=0; strcpy(d,"abcdefg"); outcome(errno?REFUSED:(strcmp(d,"abcdefg")?3:ACCEPTED)); }
static void strcpy_over(void) { char d[8]; errno=0; strcpy(d,"abcdefghijklmnop"); outcome(errno?REFUSED:3); }
static void strcpy_empty(void) { char d[4]={1,1,1,1}; errno=0; strcpy(d,""); outcome(errno?REFUSED:(d[0]?3:ACCEPTED)); }
static void strcpy_self(void) { char d[16]="hello"; errno=0; strcpy(d,d); outcome(errno?REFUSED:(strcmp(d,"hello")?3:ACCEPTED)); }
static void strcpy_null(void) { char d[4]; errno=0; strcpy(d,NULL); outcome(errno?REFUSED:3); }

static void strnlen_bounded(void) { char b[8]={'a','b','c','d','e','f','g','h'}; size_t n=strnlen(b,8); outcome(n==8?ACCEPTED:3); }
static void strnlen_zero(void) { char b[8]={0}; outcome(strnlen(b,0)==0?ACCEPTED:3); }
static void strnlen_embedded(void) { char b[8]={'a','b',0,'d'}; outcome(strnlen(b,8)==2?ACCEPTED:3); }
static void strnlen_null(void) { errno=0; size_t n=strnlen(NULL,4); outcome(errno?REFUSED:(n==0?ACCEPTED:3)); }

/* ---- I/O methods -------------------------------------------------------- */
static void fread_valid(void) { FILE*f=tmpfile(); if(!f)outcome(3); fwrite("abc",1,3,f); rewind(f); char b[8]; size_t n=fread(b,1,3,f); fclose(f); outcome(n==3?ACCEPTED:3); }
static void fread_overflow(void) { FILE*f=tmpfile(); if(!f)outcome(3); char b[8]; errno=0; size_t n=fread(b,(size_t)-1,2,f); fclose(f); outcome(n==0&&errno?REFUSED:3); }
static void fread_null(void) { FILE*f=tmpfile(); if(!f)outcome(3); errno=0; size_t n=fread(NULL,1,1,f); fclose(f); outcome(n==0&&errno?REFUSED:3); }
static void fwrite_overflow(void) { FILE*f=tmpfile(); if(!f)outcome(3); char b[8]; errno=0; size_t n=fwrite(b,(size_t)-1,2,f); fclose(f); outcome(n==0&&errno?REFUSED:3); }

struct Case { const char *method; const char *scenario; void (*fn)(void); };
static const struct Case CASES[] = {
 {"malloc","valid",malloc_valid},{"malloc","zero",malloc_zero},{"malloc","huge",malloc_huge},{"malloc","reuse",malloc_reuse},
 {"calloc","valid",calloc_valid},{"calloc","zero",calloc_zero},{"calloc","overflow",calloc_overflow},{"calloc","large",calloc_huge},
 {"realloc","grow",realloc_grow},{"realloc","shrink",realloc_shrink},{"realloc","huge-preserve",realloc_huge_preserve},{"realloc","invalid",realloc_invalid},
 {"free","valid",free_valid},{"free","null",free_null},{"free","double-alias",free_double_alias},{"free","foreign",free_foreign},
 {"memset","exact",memset_exact},{"memset","zero",memset_zero},{"memset","over",memset_over},{"memset","one-past",memset_end},{"memset","null",memset_null},
 {"memcpy","exact",memcpy_exact},{"memcpy","zero",memcpy_zero},{"memcpy","over",memcpy_over},{"memcpy","overlap",memcpy_overlap},{"memcpy","null",memcpy_null},
 {"strcpy","exact",strcpy_exact},{"strcpy","over",strcpy_over},{"strcpy","empty",strcpy_empty},{"strcpy","self",strcpy_self},{"strcpy","null",strcpy_null},
 {"strnlen","bounded",strnlen_bounded},{"strnlen","zero",strnlen_zero},{"strnlen","embedded-nul",strnlen_embedded},{"strnlen","null",strnlen_null},
 {"fread","valid",fread_valid},{"fread","overflow",fread_overflow},{"fread","null",fread_null},{"fwrite","overflow",fwrite_overflow},
};

int main(void) {
#ifndef PLAIN_LIBC
    cplus_runtime_set_violation_trap(0);
#endif
    size_t accepted=0, refused=0, crash=0, other=0;
    for (size_t i=0;i<sizeof CASES/sizeof CASES[0];++i) {
        fflush(stdout);
        pid_t pid=fork();
        if(pid==0){ CASES[i].fn(); _exit(0); }
        int st=0; waitpid(pid,&st,0);
        const char *tag;
        if(WIFEXITED(st)&&WEXITSTATUS(st)==0){tag="ACCEPTED";++accepted;}
        else if(WIFEXITED(st)&&WEXITSTATUS(st)==REFUSED){tag="REFUSED";++refused;}
        else if(WIFSIGNALED(st)){tag="CRASH";++crash;}
        else {tag="OTHER";++other;}
        printf("rustlike: %-8s %-14s %s\n",CASES[i].method,CASES[i].scenario,tag);
    }
    printf("rustlike summary: %zu accepted, %zu refused, %zu crash, %zu other\n",accepted,refused,crash,other);
    return (crash||other)?1:0;
}
