#include <string.h>
int main(void) { char data[] = "12345678"; memcpy(data + 2, data, 5); return 0; }
