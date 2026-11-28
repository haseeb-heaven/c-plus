#include <sys/mman.h>
int main(void){ void*addr=mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); if(addr!=MAP_FAILED){munmap(addr,4096);} return 0; }
