#include <stdio.h>
#include <stdarg.h>

/* Real-world style: variadic helpers plus a libc variadic call. */
static long long describe(const char *tag, ...) {
  va_list ap; int i; double d; long l; const char *s;
  va_start(ap, tag);
  i = va_arg(ap, int); d = va_arg(ap, double);
  l = va_arg(ap, long); s = va_arg(ap, const char *);
  va_end(ap);
  printf("d=%s,%d,%.2f,%ld,%s\n", tag, i, d, l, s);
  return (long long)i + (long long)l;
}
static int eight(int n, ...) {
  va_list ap; int total = 0, k;
  va_start(ap, n);
  for (k = 0; k < n; k++) total += va_arg(ap, int);
  va_end(ap); return total;
}
int main(void) {
  char buf[64]; int n;
  printf("sum=%d,%d\n", eight(8, 1, 2, 3, 4, 5, 6, 7, 8), eight(1, 42));
  printf("ret=%lld\n", describe("v", 7, 2.5, 100L, "ok"));
  n = snprintf(buf, sizeof buf, "fmt=%d/%.1f/%s", 3, 1.5, "z");
  printf("snp=%d:%s\n", n, buf);
  return 0;
}
