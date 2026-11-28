#include <stdlib.h>
static void auto_free(void*p){ free(*(void**)p); }
#define AUTO_FREE __attribute__((cleanup(auto_free)))
int* make_int(void){ AUTO_FREE int*p=(int*)malloc(sizeof(int)); *p=42; return p; }
int main(void){ int*val=make_int(); *val=10; return 0; }
