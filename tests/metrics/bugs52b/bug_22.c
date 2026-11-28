#include <stdlib.h>
struct RefObj { int refcount; int* payload; };
void release(struct RefObj*obj){ obj->refcount--; if(obj->refcount<=0){free(obj->payload);free(obj);} }
int main(void){ struct RefObj*o=(struct RefObj*)malloc(sizeof(struct RefObj)); o->refcount=1; o->payload=(int*)malloc(sizeof(int)); release(o); release(o); return 0; }
