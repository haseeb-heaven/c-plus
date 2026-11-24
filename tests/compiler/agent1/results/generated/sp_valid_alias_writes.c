extern void cplus_check_nonnull(const void *, const char *, int, const char *);
#if defined(__GNUC__) || defined(__clang__)
#define CPLUS_NONNULL(P) ({ __typeof__(P) __cplus_nn_p = (P); \
  cplus_check_nonnull((const void *)__cplus_nn_p, __FILE__, __LINE__, #P); \
  __cplus_nn_p; })
#else
#define CPLUS_NONNULL(P) \
  (cplus_check_nonnull((const void *)(P), __FILE__, __LINE__, #P), (P))
#endif
#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/valid_alias_writes.c"
struct S { int x; };
int main(void) {
  struct S s = {0};
  int *a = &s.x;
  int *b = &s.x;
  *CPLUS_NONNULL(a) = 1;
  *CPLUS_NONNULL(b) = 2;
  return 0;
}
