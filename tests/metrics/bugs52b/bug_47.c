#include <aio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
void start_async_read(int fd){ char local_buf; struct aiocb cb; memset(&cb,0,sizeof(cb)); cb.aio_fildes=fd; cb.aio_buf=local_buf; cb.aio_nbytes=sizeof(local_buf); aio_read(&cb); }
int main(void){ int fd=open("data.txt",O_RDONLY); if(fd>=0){start_async_read(fd);close(fd);} return 0; }
