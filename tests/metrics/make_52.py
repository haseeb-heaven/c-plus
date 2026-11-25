#!/usr/bin/env python3
"""Author the 52 user-provided memory-bug examples as a test corpus."""
import pathlib

OUT = pathlib.Path(__file__).parent / "bugs52"
OUT.mkdir(exist_ok=True)

P = {}

P[1] = """#include <stdlib.h>
int main(void) { int* p = (int*)malloc(sizeof(int)); if (p) *p = 42; return 0; }"""
P[2] = """#include <stdlib.h>
int main(void) { int* p = (int*)malloc((size_t)-1); *p = 10; free(p); return 0; }"""
P[3] = """#include <stdlib.h>
struct Node { int value; };
int main(void) { struct Node* n = NULL; n->value = 5; return 0; }"""
P[4] = """#include <string.h>
int main(void) { char* src = NULL; char dst[10]; memcpy(dst, src, 5); return 0; }"""
P[5] = """#include <stdlib.h>
#include <stdio.h>
int main(void) { int* p = (int*)malloc(sizeof(int)); *p = 99; free(p); printf("%d\\n", *p); return 0; }"""
P[6] = """#include <stdlib.h>
int main(void) { int* p = (int*)malloc(sizeof(int)); free(p); *p = 123; return 0; }"""
P[7] = """#include <stdio.h>
int* get_stack_ptr(void) { int local = 42; return &local; }
int main(void) { int* p = get_stack_ptr(); printf("%d\\n", *p); return 0; }"""
P[8] = """#include <stdio.h>
int main(void) { int* p = NULL; { int temp = 77; p = &temp; } printf("%d\\n", *p); return 0; }"""
P[9] = """#include <stdio.h>
int main(void) { int* p = NULL; if (1) { p = (int[]){10, 20, 30}; } printf("%d\\n", p[0]); return 0; }"""
P[10] = """#include <stdlib.h>
int main(void) { int* p1 = (int*)malloc(2 * sizeof(int)); p1[0] = 10; int* p2 = (int*)realloc(p1, 10000 * sizeof(int)); if (p2) { p1[0] = 20; free(p2); } return 0; }"""
P[11] = """#include <stdlib.h>
int main(void) { int* owner = (int*)malloc(sizeof(int)); int* observer = owner; free(owner); *observer = 50; return 0; }"""
P[12] = """#include <stdlib.h>
int main(void) { int* p = (int*)malloc(sizeof(int)); free(p); free(p); return 0; }"""
P[13] = """#include <stdlib.h>
int work(int error_flag) { int* buf = (int*)malloc(100 * sizeof(int)); if (!buf) return -1; if (error_flag) { return -1; } free(buf); return 0; }
int main(void) { return work(1); }"""
P[14] = """#include <stdlib.h>
struct Connection { int* rx_buf; int* tx_buf; };
int conn_init(struct Connection* c) { c->rx_buf = (int*)malloc(1024); if (!c->rx_buf) return -1; c->tx_buf = (int*)malloc((size_t)-1); if (!c->tx_buf) { return -1; } return 0; }
int main(void) { struct Connection c = {0}; conn_init(&c); return 0; }"""
P[15] = """#include <stdio.h>
int write_log(int abort_write) { FILE* f = fopen("app.log", "w"); if (!f) return -1; if (abort_write) { return -1; } fclose(f); return 0; }
int main(void) { return write_log(1); }"""
P[16] = """#include <pthread.h>
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
int critical_section(int fail) { pthread_mutex_lock(&lock); if (fail) { return -1; } pthread_mutex_unlock(&lock); return 0; }
int main(void) { return critical_section(1); }"""
P[17] = """#include <setjmp.h>
#include <stdlib.h>
jmp_buf env;
void deep_func(void) { int* temp = (int*)malloc(256); longjmp(env, 1); free(temp); }
int main(void) { if (setjmp(env) == 0) { deep_func(); } return 0; }"""
P[18] = """#include <stdlib.h>
struct String { char* data; };
struct User { struct String name; };
int main(void) { struct User* u = (struct User*)malloc(sizeof(struct User)); u->name.data = (char*)malloc(32); free(u); return 0; }"""
P[19] = """#include <stdlib.h>
struct Array { int* items; };
int main(void) { struct Array a; a.items = (int*)malloc(4 * sizeof(int)); struct Array b = a; free(a.items); free(b.items); return 0; }"""
P[20] = """int main(void) { int* p; *p = 42; return 0; }"""
P[21] = """#include <stdlib.h>
struct Holder { int* ptr; };
int main(void) { struct Holder* h = (struct Holder*)malloc(sizeof(struct Holder)); if (h->ptr != NULL) { *h->ptr = 10; } free(h); return 0; }"""
P[22] = """int main(void) { int arr[5]; arr[5] = 99; return 0; }"""
P[23] = """#include <stdlib.h>
int main(void) { int* arr = (int*)malloc(5 * sizeof(int)); arr[5] = 99; free(arr); return 0; }"""
P[24] = """int main(void) { int arr[5] = {1, 2, 3, 4, 5}; arr[-1] = 0; return 0; }"""
P[25] = """#include <stdlib.h>
int main(void) { int* arr = (int*)malloc(5 * sizeof(int)); arr[-1] = 0; free(arr); return 0; }"""
P[26] = """#include <stdlib.h>
int main(void) { int* buf = (int*)malloc(10 * sizeof(int)); for (int i = 0; i <= 10; i++) { buf[i] = i; } free(buf); return 0; }"""
P[27] = """#include <stdlib.h>
int main(void) { int x = 10; int* p = &x; free(p); return 0; }"""
P[28] = """#include <stdlib.h>
int main(void) { char* s = "constant string"; free(s); return 0; }"""
P[29] = """int main(void) { char* s = "hello"; s[0] = 'H'; return 0; }"""
P[30] = """#include <stdlib.h>
int main(void) { int* p = (int*)malloc(5 * sizeof(int)); p++; free(p); return 0; }"""
P[31] = """#include <stdlib.h>
int main(void) { int* p = (int*)malloc(sizeof(int)); p = (int*)malloc(sizeof(int)); free(p); return 0; }"""
P[32] = """#include <stdlib.h>
int main(void) { void* p = malloc(128); p = realloc(p, (size_t)-1); free(p); return 0; }"""
P[33] = """#include <stdio.h>
#include <stdlib.h>
int main(void) { int* p = (int*)malloc(sizeof(int)); printf("%d\\n", *p); free(p); return 0; }"""
P[34] = """#include <stdio.h>
int main(void) { int x; int y = x + 1; printf("%d\\n", y); return 0; }"""
P[35] = """#include <string.h>
void zero_buf(int* buf) { memset(buf, 0, sizeof(buf)); }
int main(void) { int data[10]; zero_buf(data); return 0; }"""
P[36] = """#include <stdlib.h>
int main(void) { int* arr = (int*)malloc(10); arr[5] = 42; free(arr); return 0; }"""
P[37] = """#include <stdlib.h>
#include <string.h>
int main(void) { const char* src = "hello"; char* dst = (char*)malloc(strlen(src)); strcpy(dst, src); free(dst); return 0; }"""
P[38] = """#include <stdio.h>
#include <string.h>
int main(void) { char buf[5]; memcpy(buf, "hello", 5); size_t len = strlen(buf); printf("%zu\\n", len); return 0; }"""
P[39] = """#include <string.h>
int main(void) { char small[4]; strcpy(small, "overflowing_string"); return 0; }"""
P[40] = """#include <string.h>
int main(void) { char data[] = "12345678"; memcpy(data + 2, data, 5); return 0; }"""
P[41] = """#include <stddef.h>
#include <stdlib.h>
int main(void) { size_t n = (size_t)-1 / 2 + 2; int* p = (int*)malloc(n * sizeof(int)); if (p) { p[0] = 1; free(p); } return 0; }"""
P[42] = """#include <string.h>
void copy_bytes(char* dst, const char* src, int len) { if (len < 16) { memcpy(dst, src, (size_t)len); } }
int main(void) { char dst[16]; char src[16] = {0}; copy_bytes(dst, src, -5); return 0; }"""
P[43] = """int main(void) { int arr[4] = {1, 2, 3, 4}; int* p = arr + 6; return 0; }"""
P[44] = """int main(void) { int arr[4] = {1, 2, 3, 4}; int* end = arr + 4; int x = *end; return x; }"""
P[45] = """#include <stdio.h>
int main(void) { float f = 1.0f; int* p = (int*)&f; *p = 0; printf("%f\\n", f); return 0; }"""
P[46] = """#include <stdlib.h>
int main(void) { char* raw = (char*)malloc(16); int* unaligned = (int*)(raw + 1); *unaligned = 42; free(raw); return 0; }"""
P[47] = """#include <stdio.h>
int main(void) { FILE* f = fopen("missing_file_xyz.txt", "r"); fclose(f); return 0; }"""
P[48] = """#include <stdio.h>
int main(void) { FILE* f = fopen("temp.txt", "w"); if (f) { fclose(f); fprintf(f, "data"); fclose(f); } return 0; }"""
P[49] = """#include <stdlib.h>
int main(void) { int** grid = (int**)malloc(3 * sizeof(int*)); for (int i = 0; i < 3; i++) grid[i] = (int*)malloc(3 * sizeof(int)); free(grid); for (int i = 0; i < 3; i++) { free(grid[i]); } return 0; }"""
P[50] = """#include <stdlib.h>
struct Message { int length; char payload[]; };
int main(void) { struct Message* msg = (struct Message*)malloc(sizeof(struct Message)); msg->payload[0] = 'X'; free(msg); return 0; }"""
P[51] = """#include <stdlib.h>
int main(void) { int* p = (int*)malloc(0); if (p != NULL) { *p = 10; free(p); } return 0; }"""
P[52] = """#include <stdio.h>
void attach_buffer(FILE* f) { char local_buf[BUFSIZ]; setbuf(f, local_buf); }
int main(void) { attach_buffer(stdout); printf("Writing into destroyed stack memory\\n"); return 0; }"""

for n, body in P.items():
    (OUT / f"bug_{n:02d}.c").write_text(body + "\n")
print(f"wrote {len(P)} bug examples")
