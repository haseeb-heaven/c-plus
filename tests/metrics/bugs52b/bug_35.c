#include <stddef.h>
#define container_of(ptr,type,member) ((type*)((char*)(ptr)-offsetof(type,member)))
struct Item { int id; int node; };
int main(void){ int*member_ptr=NULL; struct Item*item=container_of(member_ptr,struct Item,node); item->id=1; return 0; }
