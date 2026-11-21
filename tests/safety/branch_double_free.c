#include <stdlib.h>
int main(int argc,char**argv){ int *p=malloc(16); if(!p)return 1; if(argc>1) free(p); free(p); return 0; }  // NOSONAR intentional unsafe fixture for the C+ safe-mode checker
