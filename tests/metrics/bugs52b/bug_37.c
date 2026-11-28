#include <stdlib.h>
void alloc_buffer(char**out){ *out=(char*)malloc(64); }
int main(void){ char*p=NULL; alloc_buffer(&p); alloc_buffer(&p); free(p); return 0; }
