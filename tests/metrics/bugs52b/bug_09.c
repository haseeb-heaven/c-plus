#include <stdio.h>
#include <string.h>
const char* format_id(int id){ static char buf[32]; snprintf(buf,sizeof(buf),"ID-%d",id); return buf; }
int main(void){ const char*a=format_id(1); const char*b=format_id(2); printf("%s vs %s\n",a,b); return 0; }
