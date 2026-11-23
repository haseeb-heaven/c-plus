/* Native C+ safety test harness.
 *
 * Compiled with the C+ driver itself. It writes each embedded scenario to a
 * temporary source file, invokes `c+ check --strict` and
 * `c+ --strict --sanitize=none build`, then asserts the exact diagnostic or
 * clean acceptance. No Python is used by this test path.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  const char *name;
  const char *src;
  const char *expected; /* NULL means accept */
} Case;

static const Case cases[] = {
{"arith_add", "#include <limits.h>\nint main(void){int v=INT_MAX;return v+1;}\n", "signed integer overflow in addition"},
{"arith_sub", "#include <limits.h>\nint main(void){int v=INT_MIN;return v-1;}\n", "signed integer overflow in subtraction"},
{"arith_mul", "#include <limits.h>\nint main(void){int v=INT_MAX;return v*2;}\n", "signed integer overflow in multiplication"},
{"arith_div_zero", "int main(void){int z=0;return 1/z;}\n", "division by zero"},
{"arith_mod_zero", "int main(void){int z=0;return 1%z;}\n", "remainder by zero"},
{"arith_div_unknown", "static int d(int a,int b){return a/b;}\nint main(int argc,char**argv){(void)argv;return d(100,argc-1);}\n", "division by zero cannot be excluded"},
{"arith_shift_negative", "int main(void){volatile int n=-1;return 1<<n;}\n", "negative shift count"},
{"arith_shift_wide", "int main(void){volatile int n=32;return 1<<n;}\n", "shift count 32 is not less than the width"},
{"arith_safe", "#include <limits.h>\nint main(void){int v=INT_MAX-1;int r=v+1;unsigned u=UINT_MAX;unsigned w=u+1;return (r==INT_MAX&&w==0)?0:1;}\n", NULL},
{"const_div_guarded", "int d(int n,int k){if(k)return n/k;return 0;}\nint main(void){return d(100,4)==25?0:1;}\n", NULL},
{"uaf", "#include <stdlib.h>\nint main(void){int*p=malloc(4);if(!p)return 1;free(p);return *p;}\n", "use-after-free"},
{"double_free", "#include <stdlib.h>\nint main(void){int*p=malloc(4);if(!p)return 1;free(p);free(p);return 0;}\n", "double free"},
{"null_deref", "int main(void){int*p=0;return *p;}\n", "null pointer dereference"},
{"const_oob", "int main(void){int a[4]={0};return a[7];}\n", "outside the bounds"},
{"dyn_oob_guarded", "int main(void){int a[4]={0};for(int i=0;i<4;i++)a[i]=i;return a[3]-3;}\n", NULL},
{"uninit", "int main(void){int x;int y=x;return y;}\n", "uninitialized"},
{"dangling_stack", "int main(void){int*p;{int x=1;p=&x;}*p=2;return 0;}\n", "use of pointer"},
{"return_local", "int *f(void){int x=1;return &x;}\nint main(void){return *f();}\n", "address of stack memory"},
{"field_uaf", "#include <stdlib.h>\nstruct S{int*p;};\nint main(void){struct S s;s.p=malloc(8);if(!s.p)return 1;free(s.p);return *s.p;}\n", "use-after-free"},
{"alias_uaf", "#include <stdlib.h>\nint main(void){int*p=malloc(8);if(!p)return 1;int*q=p;free(p);return *q;}\n", "use-after-free"},
{"alias_safe", "#include <stdlib.h>\nint main(void){int*p=malloc(8);if(!p)return 1;int*q=p;*q=1;free(p);return 0;}\n", NULL},
{"alias_mutate", "int main(void){int v=1;int*a=&v;int*b=&v;*a=2;*b=3;return v;}\n", "exclusive mutable borrow violated"},
{"fam_oob", "#include <stdlib.h>\nstruct V{int n;int a[];};\nint main(void){struct V*v=malloc(sizeof *v+8);if(!v)return 1;return v->a[2];}\n", "flexible array index"},
{"strncpy_src", "#include <string.h>\nint main(void){char s[4]=\"abc\",d[16];strncpy(d,s,16);return 0;}\n", "exceeds source of length"},
{"compound_oob", "int main(void){int*p=(int[]){1,2};return p[3];}\n", "exceeds compound literal"},
{"double_fclose", "#include <stdio.h>\nint main(void){FILE*f=fopen(\"t\",\"w\");if(!f)return 1;fclose(f);fclose(f);return 0;}\n", "double close"},
{"checked_add_ok", "#include <limits.h>\nint checked_add(int a,int b,int*out){*out=a+b;return 1;}\nint main(void){int r;if(checked_add(INT_MAX-1,1,&r)&&r==INT_MAX)return 0;return 1;}\n", NULL},
{"format_nonliteral", "#include <stdio.h>\nint main(void){char s[16];if(fgets(s,sizeof s,stdin))printf(s);return 0;}\n", "format string in printf is not a string literal"},
{"format_n", "#include <stdio.h>\nint main(void){int n=0;printf(\"n=%n\",&n);return 0;}\n", "'%n' in printf"},
{"realloc_lost", "#include <stdlib.h>\nint main(void){int*p=malloc(8);if(!p)return 1;realloc(p,32);return 0;}\n", "raw realloc return value is ignored"},
{"realloc_ok", "#include <stdlib.h>\nint main(void){int*p=malloc(8);if(!p)return 1;int*t=realloc(p,32);if(!t){free(p);return 1;}free(t);return 0;}\n", NULL},
{"vla_unbounded", "int main(void){size_t n=0;if(scanf(\"%zu\",&n)!=1)return 1;int a[n];a[0]=1;return 0;}\n", "variable-length stack allocation"},
{"zero_alloc", "#include <stdlib.h>\nint main(void){void*p=malloc(0);(void)p;return 0;}\n", "zero-size allocation"},
{"strcpy_overflow", "#include <string.h>\nint main(void){char d[4];strcpy(d,\"abcdefgh\");return 0;}\n", "string copy of"},
{"memcpy_overlap", "#include <string.h>\nint main(void){char b[8]={0};memcpy(b,b+2,4);return 0;}\n", "overlap"},
{"decay_direct_bad", "int first(int*p){return p[4];}\nint main(void){int a[4]={0};return first(a);}\n", "array index 4 exceeds"},
{"decay_nested_bad", "static int second(int*q){return q[7];}\nstatic int first(int*p){return second(p);}\nint main(void){int a[4]={1,2,3,4};return first(a);}\n", "array index 7 exceeds"},
{"decay_nested_safe", "static int second(int*q){return q[3];}\nstatic int first(int*p){return second(p);}\nint main(void){int a[4]={1,2,3,4};return first(a);}\n", NULL},
{"decay_unknown_ptr", "int at(int*p,int i){return p[i];}\nint main(void){int*a=(int*)0;return at(a,2);}\n", "unverifiable extent"},
{"decay_returned_ptr_bad", "static int*get(int*p){return p;}\nint main(void){int a[4]={0};return get(a)[4];}\n", "array index 4 exceeds"},
{"decay_returned_ptr_ok", "static int*get(int*p){return p;}\nint main(void){int a[4]={0};return get(a)[3];}\n", NULL},
{"decay_returned_static_bad", "static int a[4];\nstatic int*get(void){return a;}\nint main(void){return get()[4];}\n", "array index 4 exceeds"},
{"decay_one_past_safe", "int main(void){int a[4]={0};int*p=a+4;return p!=0?0:1;}\n", NULL},
{"int_to_ptr", "#include <stdint.h>\nint main(void){int*p=(int*)(uintptr_t)0xdeadbeef;return *p;}\n", "unverifiable provenance"},
};

static void path_for(char *buf, size_t n, const char *suffix) {
  snprintf(buf, n, "/tmp/cplus_native_%d_%s", (int)getpid(), suffix);
}

static int run_cmd(const char *cmd, const char *outpath) {
  char full[2048];
  snprintf(full, sizeof(full), "%s > \"%s\" 2>&1", cmd, outpath);
  return system(full);
}

static int contains(const char *haystack, const char *needle) {
  return needle && strstr(haystack, needle) != NULL;
}

static char *read_all(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
  long sz = ftell(f);
  if (sz < 0) { fclose(f); return NULL; }
  rewind(f);
  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) { fclose(f); return NULL; }
  size_t got = fread(buf, 1, (size_t)sz, f);
  buf[got] = 0;
  fclose(f);
  return buf;
}

static void write_src(const char *path, const char *src) {
  FILE *f = fopen(path, "wb");
  if (f) { fputs(src, f); fclose(f); }
}

int main(void) {
  const char *cplus = getenv("CPLUS_BIN");
  if (!cplus || !*cplus) cplus = "./build/c+";
  char cmd[2048], src[256], out[256], obj[256];
  int pass = 0, fail = 0;
  size_t total = sizeof(cases) / sizeof(cases[0]);

  for (size_t i = 0; i < total; ++i) {
    const Case *c = &cases[i];
    path_for(src, sizeof(src), "case.c");
    path_for(out, sizeof(out), "case.out");
    path_for(obj, sizeof(obj), "case.bin");
    write_src(src, c->src);

    snprintf(cmd, sizeof(cmd), "CPLUS_CLANG=/usr/bin/clang \"%s\" check --strict \"%s\"", cplus, src);
    int rc1 = run_cmd(cmd, out);
    char *d1 = read_all(out);

    snprintf(cmd, sizeof(cmd), "CPLUS_CLANG=/usr/bin/clang \"%s\" --strict --sanitize=none \"%s\" -o \"%s\"", cplus, src, obj);
    int rc2 = run_cmd(cmd, out);
    char *d2 = read_all(out);

    int ok;
    if (c->expected) {
      ok = rc1 != 0 && rc2 != 0 && contains(d1, c->expected) &&
           contains(d2, c->expected);
    } else {
      ok = rc1 == 0 && rc2 == 0 && !contains(d1, "error:") &&
           !contains(d2, "error:");
    }
    printf("%s %-24s %s\n", ok ? "ok  " : "FAIL", c->name,
           c->expected ? c->expected : "accept");
    if (ok) ++pass; else ++fail;
    free(d1); free(d2);
    unlink(src); unlink(out); unlink(obj);
  }
  printf("native C+ safety suite: %d/%d passed\n", pass, (int)total);
  return fail ? 1 : 0;
}
