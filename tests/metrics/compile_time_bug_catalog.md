# Compile-time bug catalog — 100 minimal must-reject programs

## 1. use-after-free (10)

| # | ID | Minimal source (one line) | Diag | Mode | Gap |
| --- | --- | --- | --- | --- | --- |
| 1 | UAF-01 | `int *p=malloc(4); free(p); return *p;` | LIFETIME | DEFAULT error | — |
| 2 | UAF-02 | `int *p=malloc(4), *q=p; free(p); *q=1;` | LIFETIME | DEFAULT error | — |
| 3 | UAF-03 | `int *p=malloc(4), *q=p, *r=q; free(p); *r=1;` | LIFETIME | DEFAULT error | — |
| 4 | UAF-04 | `int *p=malloc(4); if(c) free(p); return *p;` | LIFETIME | STRICT-only | GAP: possible-UAF warned in DEFAULT |
| 5 | UAF-05 | `struct S{int x;}; struct S *p=malloc(sizeof *p); free(p); return p->x;` | LIFETIME | DEFAULT error | — |
| 6 | UAF-06 | `int *p=malloc(4); free(p); return p[0];` | LIFETIME | DEFAULT error | — |
| 7 | UAF-07 | `int *p=malloc(4); free(p); cplus_check_live(p);` | LIFETIME | DEFAULT error | — |
| 8 | UAF-08 | `int *f(void){int *p=malloc(4);free(p);return p;} int main(void){int *q=f();*q=3;}` | LIFETIME | DEFAULT error | — |
| 9 | UAF-09 | `int *p=malloc(4), *q=p; void *t=realloc(p,8); if(t){p=t;return *q;} free(p);` | LIFETIME | DEFAULT error | — |
| 10 | UAF-10 | `struct S{int x;}; struct S *p=malloc(sizeof *p), *f=p; free(p); return f->x;` | LIFETIME | DEFAULT error | — |

## 2. double free / invalid release (10)

| # | ID | Minimal source (one line) | Diag | Mode | Gap |
| --- | --- | --- | --- | --- | --- |
| 11 | DF-01 | `int *p=malloc(4); free(p); free(p);` | OWNER | DEFAULT error | — |
| 12 | DF-02 | `int *p=malloc(4), *q=p; free(p); free(q);` | OWNER | DEFAULT error | — |
| 13 | DF-03 | `void c(int *p){free(p);} int main(void){int *p=malloc(4);c(p);c(p);}` | OWNER | DEFAULT error | — |
| 14 | DF-04 | `int *p=malloc(4); if(c) free(p); free(p);` | OWNER | DEFAULT error | — |
| 15 | DF-05 | `int *p=malloc(4), *q=p, *r=q; free(p); free(r);` | OWNER | DEFAULT error | — |
| 16 | DF-06 | `int *p=cplus_alloc(4); cplus_release(p); cplus_release(p);` | OWNER | DEFAULT error | — |
| 17 | DF-07 | `int *p=cplus_new(int), *q=p; cplus_release(p); cplus_release(q);` | OWNER | DEFAULT error | — |
| 18 | DF-08 | `static int *g; void *w(void*a){free(g);return 0;} int main(void){pthread_create(&t,0,w,0);pthread_create(&t2,0,w,0);}` | OWNER | DEFAULT error | — |
| 19 | DF-09 | `int *p=malloc(4), *q=realloc(p,8); free(p); free(q);` | REALLOC | STRICT-only | GAP: only unverified-realloc warning; double free unproven |
| 20 | DF-10 | `struct S{int *x;}; struct S s; s.x=malloc(4); free(s.x); free(s.x);` | OWNER | STRICT-only | GAP: field-owned pointer untracked; no diagnostic |

## 3. invalid frees (stack, global, string literal, array, interior pointer, field, cast) (10)

| # | ID | Minimal source (one line) | Diag | Mode | Gap |
| --- | --- | --- | --- | --- | --- |
| 21 | IF-01 | `int x; free(&x);` | OWNER | DEFAULT error | — |
| 22 | IF-02 | `static int g; int main(void){free(&g);}` | OWNER | DEFAULT error | — |
| 23 | IF-03 | `free("abc");` | OWNER | DEFAULT error | — |
| 24 | IF-04 | `const char *s="abc"; free((void *)s);` | OWNER | DEFAULT error | — |
| 25 | IF-05 | `char a[8]; free(a);` | OWNER | DEFAULT error | — |
| 26 | IF-06 | `int *p=malloc(40); free(p+1);` | OWNER | DEFAULT error | — |
| 27 | IF-07 | `int a[4]; free(&a[2]);` | OWNER | DEFAULT error | — |
| 28 | IF-08 | `struct S{int x;}; struct S s; free(&s.x);` | OWNER | DEFAULT error | — |
| 29 | IF-09 | `int *p=malloc(8); free((int *)((char *)p+4));` | OWNER | DEFAULT error | — |
| 30 | IF-10 | `int a[4], *p=&a[2]; free(p);` | OWNER | DEFAULT error | — |

## 4. buffer overflows (arrays, pointer arithmetic, memcpy/memset/strcpy/strncpy/strcat/snprintf) (10)

| # | ID | Minimal source (one line) | Diag | Mode | Gap |
| --- | --- | --- | --- | --- | --- |
| 31 | BO-01 | `int a[4]; a[10]=1;` | BOUNDS | DEFAULT error | — |
| 32 | BO-02 | `int a[4]; int i=10; a[i]=1;` | BOUNDS | DEFAULT error | — |
| 33 | BO-03 | `int a[4]; for(int i=0;i<5;i++) a[i]=1;` | BOUNDS | DEFAULT error | — |
| 34 | BO-04 | `int a[4], *p=a+8; return *p;` | BOUNDS | DEFAULT error | — |
| 35 | BO-05 | `char d[4]; memcpy(d,"abcdefgh",8);` | BOUNDS | DEFAULT error | — |
| 36 | BO-06 | `char d[4]; memset(d,0,10);` | BOUNDS | DEFAULT error | — |
| 37 | BO-07 | `char d[4]; strcpy(d,"abcdefgh");` | BOUNDS | DEFAULT error | — |
| 38 | BO-08 | `char d[4]; strncpy(d,"abcdefgh",8);` | BOUNDS | DEFAULT error | — |
| 39 | BO-09 | `char d[4]="a"; strcat(d,"abcdefgh");` | BOUNDS | DEFAULT error | — |
| 40 | BO-10 | `char d[4]; snprintf(d,100,"%s","abcdefgh");` | BOUNDS | DEFAULT error | — |

## 5. null dereference / uninitialized reads (10)

| # | ID | Minimal source (one line) | Diag | Mode | Gap |
| --- | --- | --- | --- | --- | --- |
| 41 | ND-01 | `int *p=0; return *p;` | LIFETIME | DEFAULT error | — |
| 42 | ND-02 | `struct S{int x;}; struct S *p=0; p->x=1;` | LIFETIME | DEFAULT error | — |
| 43 | ND-03 | `int *p=0; p[0]=1;` | LIFETIME | DEFAULT error | — |
| 44 | ND-04 | `int x; if(c) x=7; return x;` | LIFETIME | DEFAULT error | — |
| 45 | ND-05 | `int x; printf("%d",x);` | LIFETIME | DEFAULT error | — |
| 46 | ND-06 | `int *p; return *p;` | LIFETIME | DEFAULT error | — |
| 47 | ND-07 | `int *p=0, *q=p; return *q;` | LIFETIME | STRICT-only | GAP: null state not propagated through alias |
| 48 | ND-08 | `int *p=malloc(4); *p=1;` | LIFETIME | STRICT-only | GAP: unchecked allocator NULL not diagnosed |
| 49 | ND-09 | `struct S{int a,b;}; struct S s; return s.a;` | LIFETIME | STRICT-only | GAP: field-level definite init untracked |
| 50 | ND-10 | `int x; x=x+1; return x;` | LIFETIME | DEFAULT error | — |

## 6. provenance (integer-forged pointers, bad casts) (10)

| # | ID | Minimal source (one line) | Diag | Mode | Gap |
| --- | --- | --- | --- | --- | --- |
| 51 | PV-01 | `int *p=(int *)0x1234; *p=1;` | FFI | STRICT-only | — |
| 52 | PV-02 | `void *p=(void *)0xDEAD; free(p);` | FFI | STRICT-only | — |
| 53 | PV-03 | `((char *)0x1000)[0]=1;` | FFI | STRICT-only | — |
| 54 | PV-04 | `uintptr_t a=0x1234; int *p=(int *)a; *p=1;` | FFI | STRICT-only | — |
| 55 | PV-05 | `int x; free((void *)&x);` | OWNER | DEFAULT error | — |
| 56 | PV-06 | `double d=1; int *p=(int *)&d; *p=1;` | FFI | DEFAULT error | — |
| 57 | PV-07 | `int *p=(int *)-1; return *p;` | FFI | STRICT-only | — |
| 58 | PV-08 | `char *p=(char *)0; return p[0];` | LIFETIME | DEFAULT error | — |
| 59 | PV-09 | `int *p=(int *)0x10; free(p+1);` | OWNER | DEFAULT error | — |
| 60 | PV-10 | `int a[4]; free((void *)&a[1]);` | OWNER | DEFAULT error | — |

## 7. ownership / moves / resource escapes (10)

| # | ID | Minimal source (one line) | Diag | Mode | Gap |
| --- | --- | --- | --- | --- | --- |
| 61 | OW-01 | `resource struct F{int h;}; void F_drop(F*f){(void)f;} int main(void){F a; F b=a;}` | OWNER | DEFAULT error | — |
| 62 | OW-02 | `resource struct F{int h;}; void F_drop(F*f){(void)f;} int main(void){F a,b; b=a;}` | OWNER | DEFAULT error | — |
| 63 | OW-03 | `resource struct H{int id;}; void H_drop(H*h){(void)h;} int main(void){H a={1}; H b=cplus_move(a); return a.id;}` | OWNER | DEFAULT error | — |
| 64 | OW-04 | `resource struct H{int id;}; void H_drop(H*h){(void)h;} int main(void){H a={1}; H *b=&a; H c=cplus_move(a); return b->id;}` | OWNER | DEFAULT error | — |
| 65 | OW-05 | `resource struct F{FILE*h;}; void F_drop(F*f){(void)f;} FILE *bad(void){F f; f.h=fopen("x","w"); return f.h;}` | BORROW | DEFAULT error | — |
| 66 | OW-06 | `managed int *p=cplus_new(int); return p;` | OWNER | DEFAULT error | — |
| 67 | OW-07 | `resource struct H{int id;}; void H_drop(H*h){(void)h;} int main(void){H a={1}; H b=cplus_move(a), c=cplus_move(a);}` | OWNER | STRICT-only | GAP: double move is warning-only |
| 68 | OW-08 | `owned int x=1;` | OWNER | DEFAULT error | — |
| 69 | OW-09 | `resource struct H{int id;}; void H_drop(H*h){(void)h;} int main(void){H a={1}; H b=cplus_move(a); return a.id+b.id;}` | OWNER | DEFAULT error | — |
| 70 | OW-10 | `struct S{int *x;}; struct S a; a.x=malloc(4); struct S b=a; free(b.x); return *a.x;` | ALIAS | STRICT-only | GAP: shallow field copy not treated as ownership move |

## 8. realloc failure/success mistakes (10)

| # | ID | Minimal source (one line) | Diag | Mode | Gap |
| --- | --- | --- | --- | --- | --- |
| 71 | RE-01 | `int *p=malloc(4); void *t=realloc(p,8); (void)t; free(p);` | REALLOC | STRICT-only | — |
| 72 | RE-02 | `int *p=malloc(4), *q=p; void *t=realloc(p,8); if(t){p=t;return *q;} free(p);` | LIFETIME | DEFAULT error | — |
| 73 | RE-03 | `int *p=malloc(4), *q=realloc(p,8); free(p); free(q);` | REALLOC | STRICT-only | GAP: double free after unverified realloc unproven |
| 74 | RE-04 | `int *p=malloc(4), *q=realloc(p,8); *p=1; free(q);` | REALLOC | STRICT-only | GAP: stale-alias use after unchecked success unproven |
| 75 | RE-05 | `int *p=malloc(4); realloc(p,8);` | REALLOC | DEFAULT error | — |
| 76 | RE-06 | `int *p=malloc(4); p=realloc(p,8);` | REALLOC | DEFAULT error | — |
| 77 | RE-07 | `int *p=malloc(4); void *t=realloc(p,8); if(t){free(p);p=t;} free(p);` | OWNER | DEFAULT error | — |
| 78 | RE-08 | `int *p=malloc(4); void *t=realloc(p,8); if(!t){free(p);return 1;} p=t; free(p); free(p);` | OWNER | DEFAULT error | — |
| 79 | RE-09 | `int *p=cplus_alloc(4), *q=cplus_realloc(p,8); cplus_release(p); cplus_release(q);` | REALLOC | STRICT-only | GAP: double release after unverified cplus_realloc unproven |
| 80 | RE-10 | `int *p=malloc(4); void *t=realloc(p,8); if(t){} else free(p); return *p;` | LIFETIME | STRICT-only | — |

## 9. thread/FFI/shared-state hazards (10)

| # | ID | Minimal source (one line) | Diag | Mode | Gap |
| --- | --- | --- | --- | --- | --- |
| 81 | TH-01 | `pthread_create(&t,0,w,&local);` | FFI | STRICT-only | — |
| 82 | TH-02 | `static int g; void *w(void*a){g++;return 0;} int main(void){pthread_create(&a,0,w,0);pthread_create(&b,0,w,0);}` | FFI | DEFAULT error | — |
| 83 | TH-03 | `static volatile int g; void *w(void*a){g++;return 0;} int main(void){pthread_create(&a,0,w,0);pthread_create(&b,0,w,0);}` | FFI | STRICT-only | — |
| 84 | TH-04 | `static int *g; void *w(void*a){free(g);return 0;} int main(void){pthread_create(&a,0,w,0);pthread_create(&b,0,w,0);}` | OWNER | DEFAULT error | — |
| 85 | TH-05 | `static int g; void h(int s){g=1;} int main(void){signal(SIGINT,h);}` | FFI | DEFAULT error | — |
| 86 | TH-06 | `void h(int s){printf("x");} int main(void){signal(SIGINT,h);}` | FFI | DEFAULT error | — |
| 87 | TH-07 | `jmp_buf j; void f(void){longjmp(j,1);}` | RAII | STRICT-only | GAP: longjmp cleanup bypass only warned |
| 88 | TH-08 | `extern void consume(int*); int *p=malloc(4); consume(p); consume(p);` | OWNER | STRICT-only | GAP: unknown FFI effects not summarized |
| 89 | TH-09 | `static int g; void *w(void*a){g++;return 0;} int main(void){pthread_create(&a,0,w,0);}` | FFI | STRICT-only | GAP: single-worker unsynchronised global write not flagged |
| 90 | TH-10 | `static void **s; void *w(void*a){*s=a;return 0;} int main(void){int x=1;pthread_create(&t,0,w,&x);pthread_join(t,0);return **(int**)s;}` | FFI | STRICT-only | GAP: escaped stack borrow not tracked |

## 10. contract violations (null args to libc, wrong sizes, unchecked stateful APIs) (10)

| # | ID | Minimal source (one line) | Diag | Mode | Gap |
| --- | --- | --- | --- | --- | --- |
| 91 | CT-01 | `strlen(0);` | LIFETIME | DEFAULT error | — |
| 92 | CT-02 | `char d[4]; memcpy(d,0,4);` | LIFETIME | DEFAULT error | — |
| 93 | CT-03 | `strcmp(0,"a");` | LIFETIME | DEFAULT error | — |
| 94 | CT-04 | `char d[4]; memcmp(0,d,4);` | LIFETIME | DEFAULT error | — |
| 95 | CT-05 | `char d[4]="a"; strcat(d,0);` | LIFETIME | DEFAULT error | — |
| 96 | CT-06 | `char d[4]; cplus_strcpy(d,2,"abc");` | LIBC | DEFAULT error | — |
| 97 | CT-07 | `int *p=malloc(3); p[0]=1;` | BOUNDS | STRICT-only | GAP: wrong malloc byte count with compatible type unproven |
| 98 | CT-08 | `FILE *f=fopen("x","r"); return f->_flag;` | LIFETIME | STRICT-only | GAP: unchecked fopen NULL not diagnosed |
| 99 | CT-09 | `char d[8]; fread(d,1,8,fp);` | FFI | DEFAULT error | — |
| 100 | CT-10 | `char d[8]="abcdef"; memcpy(d,d+1,4);` | FFI | DEFAULT error | — |
