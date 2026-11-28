
#include <stdlib.h>
static int *g;
static void setg(int *p){ g = p; }
static void freeg(void){ free(g); }
int main(void){ int *p=malloc(sizeof(int)); setg(p); freeg(); return *p; }
