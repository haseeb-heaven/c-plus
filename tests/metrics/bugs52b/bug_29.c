#include <stdio.h>
int main(void){ char buf; int needed=snprintf(buf,sizeof(buf),"LongStringExceedingBuffer"); buf[needed]='\0'; return 0; }
