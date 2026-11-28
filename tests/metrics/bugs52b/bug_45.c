#include <stdlib.h>
struct FreeSlot { struct FreeSlot* next; };
struct FreeSlot* free_list=NULL;
void pool_free(void*ptr){ struct FreeSlot*slot=(struct FreeSlot*)ptr; slot->next=free_list; free_list=slot; }
int main(void){ void*block=malloc(32); pool_free(block); pool_free(block); free(block); return 0; }
