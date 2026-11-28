#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/c_apps/apps/hash_tool.c"
/* Original deterministic FNV-1a hash tool (plain C89). */
#include <stdio.h>
#include <stdint.h>

static uint32_t fnv1a(const char *s) {
  uint32_t h = 2166136261u;
  int i = 0;
  while (s[i] != '\0') {
    h = h ^ (uint32_t)(unsigned char)s[i];
    h = h * 16777619u;
    i++;
  }
  return h;
}

int main(void) {
  const char *samples[3] = {"alpha", "beta", "gamma"};
  int i;
  for (i = 0; i < 3; i++)
    printf("%s=%08lx\n", samples[i], (unsigned long)fnv1a(samples[i]));
  return 0;
}
