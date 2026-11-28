#include <string.h>
struct Header { char flag; int count; };
int main(void){ struct Header h1,h2; h1.flag=1;h1.count=10;h2.flag=1;h2.count=10; int same=memcmp(&h1,&h2,sizeof(struct Header)); return same; }
