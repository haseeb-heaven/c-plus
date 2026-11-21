#include <sys/mman.h>
int main(void){ void*addr=mmap(NULL,0,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); if(addr!=NULL){*(int*)addr=1;munmap(addr,0);} return 0; }
