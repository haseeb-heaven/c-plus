#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
int main(void){ int fd=open("tiny.bin",O_RDWR|O_CREAT|O_TRUNC,0600); ftruncate(fd,10); char*map=(char*)mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0); close(fd); if(map!=MAP_FAILED){*map='Z';munmap(map,8192);} return 0; }
