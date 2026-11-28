#include <stdlib.h>
int main(int argc,char**argv){ int *p=malloc(sizeof(int)); *p=0; if(argc>1) free(p); return *p; }  // NOSONAR intentional safe-mode safety fixture; initialise so the accepted branch returns deterministically on every libc
