#!/usr/bin/env python3
"""Author the curated safe-C acceptance corpus (run once, files are committed).

These are ordinary ISO C programs covering idiomatic patterns. They contain no
C+ extensions, no annotations and no renames. The generator is only an authoring
aid: the .c files are the corpus.
"""
import pathlib

OUT = pathlib.Path(__file__).parent / "safe_corpus"
OUT.mkdir(exist_ok=True)

PROGRAMS = {
"array_sum": """#include <stddef.h>
int main(void) { int a[8] = {1,2,3,4,5,6,7,8}; int s = 0;
  for (size_t i = 0; i < 8; i++) s += a[i]; return s == 36 ? 0 : 1; }""",
"matrix_mul": """#include <stddef.h>
int main(void) { int a[2][2] = {{1,2},{3,4}}, b[2][2] = {{5,6},{7,8}}, c[2][2] = {{0}};
  for (size_t i=0;i<2;i++) for (size_t j=0;j<2;j++) for (size_t k=0;k<2;k++) c[i][j]+=a[i][k]*b[k][j];
  return c[0][0]==19 && c[1][1]==50 ? 0 : 1; }""",
"string_length": """#include <stddef.h>
int main(void) { const char *s = "hello world"; size_t n = 0;
  while (s[n] != '\\0') n++; return n == 11 ? 0 : 1; }""",
"string_reverse": """#include <string.h>
int main(void) { char s[16] = "abcdef"; size_t n = strlen(s);
  for (size_t i=0,j=n-1; i<j; i++,j--) { char t=s[i]; s[i]=s[j]; s[j]=t; }
  return strcmp(s, "fedcba") == 0 ? 0 : 1; }""",
"string_compare": """int main(void) { const char *a="abc", *b="abd"; int i=0;
  while (a[i] && a[i]==b[i]) i++; return a[i] < b[i] ? 0 : 1; }""",
"string_copy_loop": """#include <stddef.h>
int main(void) { char src[8]="hello"; char dst[8]; size_t i=0;
  while ((dst[i]=src[i]) != '\\0') i++; return dst[4]=='o' ? 0 : 1; }""",
"tokenize": """#include <string.h>
int main(void) { char s[16]="one two three"; int count=0; char *p=strtok(s," ");
  while (p) { count++; p=strtok(0," "); } return count==3 ? 0 : 1; }""",
"linked_list": """#include <stdlib.h>
struct node { int v; struct node *next; };
int main(void) { struct node *head=0; for (int i=0;i<5;i++) { struct node *n=malloc(sizeof *n);
  if(!n) return 2; n->v=i; n->next=head; head=n; } int sum=0;
  for (struct node *p=head;p;p=p->next) sum+=p->v;
  while (head) { struct node *t=head; head=head->next; free(t); } return sum==10 ? 0 : 1; }""",
"stack_array": """#include <stddef.h>
int main(void) { int st[8]; size_t top=0;
  for (int i=0;i<8;i++) st[top++] = i*i;
  int sum=0; while (top>0) sum += st[--top]; return sum==140 ? 0 : 1; }""",
"queue_array": """#include <stddef.h>
int main(void) { int q[8]; size_t head=0,tail=0;
  for (int i=0;i<4;i++) q[tail++]=i;
  int sum=0; while (head<tail) sum+=q[head++]; return sum==6 ? 0 : 1; }""",
"binary_search": """#include <stddef.h>
int find(const int *a, size_t n, int x) { size_t lo=0,hi=n;
  while (lo<hi) { size_t m=lo+(hi-lo)/2; if(a[m]<x) lo=m+1; else hi=m; }
  return lo<n && a[lo]==x ? (int)lo : -1; }
int main(void){ int a[6]={1,3,5,7,9,11}; return find(a,6,7)==3 ? 0 : 1; }""",
"quicksort": """#include <stddef.h>
static void qsort_i(int *a, int lo, int hi) { if(lo>=hi) return; int p=a[hi],i=lo;
  for(int j=lo;j<hi;j++) if(a[j]<p){int t=a[i];a[i]=a[j];a[j]=t;i++;}
  int t=a[i];a[i]=a[hi];a[hi]=t; qsort_i(a,lo,i-1); qsort_i(a,i+1,hi); }
int main(void){ int a[6]={5,2,9,1,7,3}; qsort_i(a,0,5);
  return a[0]==1 && a[5]==9 ? 0 : 1; }""",
"gcd": """int gcd(int a,int b){ while(b){int t=a%b;a=b;b=t;} return a; }
int main(void){ return gcd(48,18)==6 ? 0 : 1; }""",
"prime_check": """int is_prime(int n){ if(n<2) return 0; for(int d=2;d*d<=n;d++) if(n%d==0) return 0; return 1; }
int main(void){ int c=0; for(int i=2;i<30;i++) if(is_prime(i)) c++; return c==10 ? 0 : 1; }""",
"factorial": """unsigned long long fact(int n){ unsigned long long r=1; for(int i=2;i<=n;i++) r*=i; return r; }
int main(void){ return fact(10)==3628800ULL ? 0 : 1; }""",
"fibonacci": """int main(void){ int a=0,b=1; for(int i=0;i<10;i++){int t=a+b;a=b;b=t;} return a==55 ? 0 : 1; }""",
"dynamic_array": """#include <stdlib.h>
int main(void){ size_t cap=4,n=0; int *a=malloc(cap*sizeof *a); if(!a) return 2;
  for(int i=0;i<12;i++){ if(n==cap){cap*=2; void *t=realloc(a,cap*sizeof *a); if(!t){free(a);return 2;} a=t;} a[n++]=(int)i; }
  int sum=0; for(size_t i=0;i<n;i++) sum+=a[i]; free(a); return sum==66 ? 0 : 1; }""",
"struct_point": """struct point { int x,y; };
int main(void){ struct point p={3,4}; int d2=p.x*p.x+p.y*p.y; return d2==25 ? 0 : 1; }""",
"struct_array": """struct item { int id; int qty; };
int main(void){ struct item items[3]={{1,2},{2,5},{3,1}}; int total=0;
  for(int i=0;i<3;i++) total+=items[i].qty; return total==8 ? 0 : 1; }""",
"enum_state": """enum state { IDLE, RUNNING, DONE };
int main(void){ enum state s=IDLE; for(int i=0;i<3;i++){ if(s==IDLE) s=RUNNING; else if(s==RUNNING) s=DONE; }
  return s==DONE ? 0 : 1; }""",
"function_pointer": """#include <stddef.h>
static int add(int a,int b){return a+b;} static int sub(int a,int b){return a-b;}
int main(void){ int (*ops[2])(int,int)={add,sub}; return ops[0](7,3)==10 && ops[1](7,3)==4 ? 0 : 1; }""",
"callback": """#include <stddef.h>
static int visit(int v, int (*f)(int)){ return f(v); }
static int twice(int v){ return 2*v; }
int main(void){ return visit(21,twice)==42 ? 0 : 1; }""",
"bit_ops": """int main(void){ unsigned x=0xF0; x &= 0xFF; x |= 0x0F; x ^= 0xAA; return x==0x55 ? 0 : 1; }""",
"bit_count": """int popcount(unsigned x){ int c=0; while(x){ c+=x&1u; x>>=1; } return c; }
int main(void){ return popcount(0xF0F0)==8 ? 0 : 1; }""",
"nested_loops": """int main(void){ int total=0; for(int i=0;i<4;i++) for(int j=0;j<4;j++) total += (i==j)?1:0;
  return total==4 ? 0 : 1; }""",
"early_return": """#include <stddef.h>
static int classify(int n){ if(n<0) return -1; if(n==0) return 0; return 1; }
int main(void){ return classify(-5)==-1 && classify(0)==0 && classify(5)==1 ? 0 : 1; }""",
"switch_case": """static int days(int m){ switch(m){ case 2: return 28; case 4: case 6: return 30; default: return 31; } }
int main(void){ return days(2)==28 && days(4)==30 ? 0 : 1; }""",
"do_while": """int main(void){ int i=0, s=0; do { s+=i; i++; } while(i<5); return s==10 ? 0 : 1; }""",
"pointer_swap": """static void swap(int *a,int *b){ int t=*a; *a=*b; *b=t; }
int main(void){ int x=1,y=2; swap(&x,&y); return x==2 && y==1 ? 0 : 1; }""",
"pointer_arith_ok": """#include <stddef.h>
int main(void){ int a[5]={1,2,3,4,5}; int *p=a; int s=0;
  for(size_t i=0;i<5;i++) s += *(p+i); return s==15 ? 0 : 1; }""",
"array_2d_sum": """#include <stddef.h>
int main(void){ int g[3][4]={{0}}; int s=0;
  for(size_t i=0;i<3;i++) for(size_t j=0;j<4;j++){ g[i][j]=(int)(i+j); s+=g[i][j]; }
  return s==18 ? 0 : 1; }""",
"string_format": """#include <stdio.h>
int main(void){ char buf[32]; int n=snprintf(buf,sizeof buf,"%d-%s",7,"ok");
  return n>0 && buf[0]=='7' ? 0 : 1; }""",
"file_write_read": """#include <stdio.h>
int main(void){ FILE *f=fopen("/tmp/cplus_corpus_test.txt","w+"); if(!f) return 2;
  fputs("abc",f); rewind(f); char b[8]={0}; if(!fgets(b,sizeof b,f)){fclose(f);return 2;}
  fclose(f); return b[0]=='a' && b[2]=='c' ? 0 : 1; }""",
"dynamic_string": """#include <stdlib.h>
#include <string.h>
int main(void){ const char *s="abcdef"; size_t n=strlen(s); char *d=malloc(n+1); if(!d) return 2;
  memcpy(d,s,n+1); size_t k=0; while(d[k]) k++; free(d); return k==6 ? 0 : 1; }""",
"safe_realloc": """#include <stdlib.h>
int main(void){ int *p=malloc(4*sizeof *p); if(!p) return 2;
  void *t=realloc(p,8*sizeof *p); if(t) p=t; else { free(p); return 2; }
  for(int i=0;i<8;i++) p[i]=i; int s=0; for(int i=0;i<8;i++) s+=p[i]; free(p);
  return s==28 ? 0 : 1; }""",
"null_check_chain": """#include <stdlib.h>
int main(void){ int *p=malloc(sizeof *p); if(!p) return 2; *p=9; int v=*p; free(p); return v==9?0:1; }""",
"bounded_copy": """#include <string.h>
int main(void){ char dst[16]; const char *src="short"; size_t n=strlen(src)+1;
  if(n<=sizeof dst) memcpy(dst,src,n); else return 2; return dst[4]=='t'?0:1; }""",
"checked_copy": """#include <string.h>
int main(void){ char dst[16]={0}; strncpy(dst,"abc",sizeof dst-1); return dst[2]=='c'?0:1; }""",
"memset_init": """#include <string.h>
int main(void){ int a[16]; memset(a,0,sizeof a); int s=0; for(int i=0;i<16;i++) s+=a[i]; return s==0?0:1; }""",
"variadic_sum": """#include <stdarg.h>
static int sum(int n,...){ va_list ap; va_start(ap,n); int s=0;
  for(int i=0;i<n;i++) s+=va_arg(ap,int); va_end(ap); return s; }
int main(void){ return sum(3,1,2,3)==6?0:1; }""",
"typedef_use": """#include <stddef.h>
typedef unsigned long count_t;
int main(void){ count_t n=5; count_t s=0; for(count_t i=0;i<n;i++) s+=i; return s==10?0:1; }""",
"static_helper": """#include <stddef.h>
static int square(int x){ return x*x; }
int main(void){ int s=0; for(int i=0;i<4;i++) s+=square(i); return s==14?0:1; }""",
"sizeof_array": """#include <stddef.h>
int main(void){ int a[7]; size_t n=sizeof a/sizeof a[0]; return n==7?0:1; }""",
"const_correct": """#include <stddef.h>
static int read_all(const int *a,size_t n){ int s=0; for(size_t i=0;i<n;i++) s+=a[i]; return s; }
int main(void){ int a[4]={1,2,3,4}; return read_all(a,4)==10?0:1; }""",
"pointer_to_pointer": """#include <stdlib.h>
static void alloc_int(int **out){ *out=malloc(sizeof **out); }
int main(void){ int *p=0; alloc_int(&p); if(!p) return 2; *p=11; int v=*p; free(p); return v==11?0:1; }""",
"char_class": """int main(void){ int letters=0; const char *s="a1b2c3";
  for(int i=0;s[i];i++) if((s[i]>='a'&&s[i]<='z')) letters++; return letters==3?0:1; }""",
"nested_struct": """struct inner { int v; }; struct outer { struct inner in; int w; };
int main(void){ struct outer o={{3},4}; return o.in.v+o.w==7?0:1; }""",
"array_of_structs": """#include <stddef.h>
struct pair { int a,b; };
int main(void){ struct pair p[3]={{1,2},{3,4},{5,6}}; int s=0;
  for(size_t i=0;i<3;i++) s+=p[i].a+p[i].b; return s==21?0:1; }""",
"string_builder": """#include <string.h>
int main(void){ char out[32]; size_t k=0; const char *words[3]={"a","bb","ccc"};
  for(int i=0;i<3;i++){ size_t n=strlen(words[i]); memcpy(out+k,words[i],n); k+=n; out[k]=' '; k++; }
  out[k]='\\0'; return k==8?0:1; }""",
"min_max": """int main(void){ int a[6]={9,2,7,1,8,3}; int lo=a[0],hi=a[0];
  for(int i=1;i<6;i++){ if(a[i]<lo) lo=a[i]; if(a[i]>hi) hi=a[i]; }
  return lo==1&&hi==9?0:1; }""",
"running_average": """int main(void){ int a[5]={2,4,6,8,10}; int s=0; double avg=0;
  for(int i=0;i<5;i++){ s+=a[i]; avg=(double)s/(i+1); }
  return avg==6.0?0:1; }""",
}

for name, body in PROGRAMS.items():
    (OUT / f"{name}.c").write_text(body + "\n")
print(f"wrote {len(PROGRAMS)} safe-C corpus programs to {OUT}")
