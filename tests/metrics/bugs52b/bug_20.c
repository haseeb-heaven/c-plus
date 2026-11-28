#include <stdarg.h>
#include <stdlib.h>
void free_all(void*first,...){ va_list args; va_start(args,first); void*p=first; while(p!=NULL){free(p); p=va_arg(args,void*);} va_end(args);}
int main(void){ void*a=malloc(8); void*b=malloc(8); free_all(a,b); return 0; }
