#include <stdlib.h>
#include <string.h>
int main(void){ char*buf=(char*)malloc(16); strcpy(buf,"header:payload"); char*payload_ptr=strchr(buf,':')+1; char*new_buf=(char*)realloc(buf,4096); if(new_buf){ buf=new_buf; *payload_ptr='X'; free(buf);} return 0; }
