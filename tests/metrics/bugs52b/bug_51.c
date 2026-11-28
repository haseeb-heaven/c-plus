#include <stdlib.h>
static void auto_free(void*p){ free(*(void**)p); }
#define AUTO_FREE __attribute__((cleanup(auto_free)))
int main(void){ AUTO_FREE int*ptr; if(1) return 0; ptr=(int*)malloc(sizeof(int)); return 0; }
