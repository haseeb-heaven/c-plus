#!/usr/bin/env python3
"""Generate paired C / Rust scenarios for compile-time safety comparison.

No Rust code is copied into C+; these are independently written equivalents.
Expected C+ diagnostics are asserted by tests/core/rust_compare_suite.py.
"""
from pathlib import Path

ROOT = Path(__file__).parents[2]
CDIR = ROOT / "tests/safety/rust_compare/c"
RDIR = ROOT / "tests/safety/rust_compare/rs"
CDIR.mkdir(parents=True, exist_ok=True)
RDIR.mkdir(parents=True, exist_ok=True)

H = "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n"

# name, C body, Rust source, expected C+ diagnostic substring (or "" for accept)
S = [
("uaf", H + """
int main(void){int*p=malloc(4);if(!p)return 1;free(p);return *p;}
""", """fn main(){ let mut v = Box::new(1i32); let p = &mut *v; drop(v); *p = 2; }
""", "use-after-free"),
("double_free", H + """
int main(void){int*p=malloc(4);if(!p)return 1;free(p);free(p);return 0;}
""", """fn main(){ let v = Box::new(1i32); drop(v); drop(v); }
""", "double free"),
("null_deref", H + """
int main(void){int*p=0;return *p;}
""", """const P: Option<&i32> = None; const R: &i32 = P.unwrap();
fn main(){ println!("{}", R); }
""", "null pointer dereference"),
("dangling_stack", H + """
int *f(void){int x=1;return &x;}
int main(void){int*p=f();return *p;}
""", """fn f() -> &'static i32 { let x = 1; &x }
fn main(){ println!("{}", *f()); }
""", "address of stack"),
("const_oob", H + """
int main(void){int a[4]={0};return a[7];}
""", """const A: [i32; 4] = [0; 4]; const X: i32 = A[7];
fn main(){ println!("{}", X); }
""", "constant array index 7"),
("dyn_oob", H + """
int main(void){int a[4]={0};size_t i=0;if(scanf("%zu",&i)!=1)return 1;return a[i];}
""", """fn main(){ let a = [0i32; 4]; let i: usize = std::env::args().count()-1; println!("{}", a[i]); }
""", ""),
("uninit", H + """
int main(void){int x;int y=x;return y;}
""", """fn main(){ let x: i32; let y = x; println!("{}", y); }
""", "uninitialized"),
("signed_overflow", H + """
int main(void){int x=2147483647;volatile int y=1;return x+y;}
""", """const X: i32 = i32::MAX + 1;
fn main(){ println!("{}", X); }
""", ""),
("unsigned_overflow", H + """
#include <stdint.h>
int main(void){uint32_t x=4294967295u;volatile uint32_t y=1;return x+y;}
""", """fn main(){ let x = u32::MAX; println!("{}", x.wrapping_add(1)); }
""", ""),
("data_race", "#include <pthread.h>\n" + H + """
static int g;static void*t(void*a){(void)a;g++;return 0;}
int main(void){pthread_t a,b;pthread_create(&a,0,t,0);pthread_create(&b,0,t,0);pthread_join(a,0);pthread_join(b,0);return 0;}
""", """fn main(){ let mut x = 0; std::thread::spawn(|| { x += 1; }); }
""", "unsynchronized write"),
("mutable_alias", H + """
void f(int*a,int*b){*a=1;*b=2;}
int main(void){int x=0;f(&x,&x);return x;}
""", """fn f(a: &mut i32, b: &mut i32){ *a=1; *b=2; }
fn main(){ let mut x=0; f(&mut x, &mut x); }
""", ""),
("bad_format", H + """
int main(void){char s[16];if(fgets(s,sizeof s,stdin))printf(s);return 0;}
""", """fn main(){ let s = std::env::args().nth(1).unwrap(); println!(s); }
""", "format string in printf"),
("format_arg_mismatch", H + """
int main(void){printf("%s %d\\n",42);return 0;}
""", """fn main(){ println!("{} {}", 42); }
""", "more '%' conversions"),
("strcpy_overflow", H + """
int main(void){char d[4];strcpy(d,"abcdefgh");return 0;}
""", """const D: [u8;4] = { let s=[0u8;8]; let mut d=[0u8;4]; d.copy_from_slice(&s); d };
fn main(){ println!("{:?}", D); }
""", "string copy of"),
("leak_on_return", H + """
FILE*f(void){FILE*p=fopen("t","w");if(!p)return 0;return p;}
int main(void){FILE*p=f();(void)p;return 0;}
""", """fn f() -> std::fs::File { std::fs::File::create("/tmp/t").unwrap() }
fn main(){ let _f = f(); }
""", "leaks on this return path"),
("realloc_lost", H + """
int main(void){int*p=malloc(8);if(!p)return 1;realloc(p,64);return 0;}
""", """fn main(){ let mut v = vec![1u8;8]; let _ = v.clone(); }
""", "raw realloc return value is ignored"),
("ptr_oob", H + """
int main(void){int a[4]={0};int*p=a+4;*p=1;return 0;}
""", """const A: [i32; 4] = [0; 4]; const R: &i32 = &A[4];
fn main(){ println!("{}", R); }
""", "one past the end"),
("vla_unbounded", H + """
int main(void){size_t n=0;if(scanf("%zu",&n)!=1)return 1;int a[n];a[0]=1;return 0;}
""", """fn main(){ let n: usize = 10; let v = vec![0i32; n]; }
""", "variable-length stack allocation"),
("int_to_ptr", H + """
#include <stdint.h>
int main(void){int*p=(int*)(uintptr_t)0xdeadbeef;return *p;}
""", """fn main(){ let p = 0xdeadbeefusize as *const i32; let v = *p; println!("{}", v); }
""", "unverifiable provenance"),
("field_uaf", H + """
struct S{int*p;};
int main(void){struct S s;s.p=malloc(8);if(!s.p)return 1;free(s.p);return *s.p;}
""", """struct S { p: Box<i32> }
fn main(){ let s = S{p:Box::new(1)}; let r = &s.p; drop(s); println!("{}", r); }
""", "use-after-free"),
]

for name, csrc, rsrc, diag in S:
    (CDIR / f"{name}.c").write_text(csrc)
    (RDIR / f"{name}.rs").write_text(rsrc)
print(f"generated {len(S)} paired scenarios")
