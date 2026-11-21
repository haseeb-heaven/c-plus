#include <string.h>
void copy_bytes(char* dst, const char* src, int len) { if (len < 16) { memcpy(dst, src, (size_t)len); } }
int main(void) { char dst[16]; char src[16] = {0}; copy_bytes(dst, src, -5); return 0; }
