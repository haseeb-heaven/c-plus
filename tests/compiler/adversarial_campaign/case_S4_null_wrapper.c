
#include <stddef.h>
static void deref(int *p){ *p = 1; }
static void wrap(int *p){ deref(p); }
int main(void){ wrap(NULL); return 0; }
