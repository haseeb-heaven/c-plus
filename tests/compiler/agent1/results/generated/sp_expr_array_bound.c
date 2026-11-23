#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/expr_array_bound.c"
#define BUFSIZE (1 << 20)
static int buf[BUFSIZE];
int main(void) {
  buf[100] = 7;
  return buf[100] == 7 ? 0 : 1;
}