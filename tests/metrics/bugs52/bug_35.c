#include <string.h>
void zero_buf(int* buf) { memset(buf, 0, sizeof(buf)); }
int main(void) { int data[10]; zero_buf(data); return 0; }
