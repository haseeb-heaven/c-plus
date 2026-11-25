#include <stdlib.h>
#include <string.h>
struct Blob { size_t len; char bytes[]; };
int main(void){ struct Blob*b1=(struct Blob*)malloc(sizeof(struct Blob)+10); b1->len=10; memset(b1->bytes,'A',10); struct Blob b2=*b1; (void)b2; free(b1); return 0; }
