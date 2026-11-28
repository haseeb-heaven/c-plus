#include <stdio.h>
#include <string.h>
int main(void){ char buf; FILE*f=fopen("data.txt","rb"); if(f){ size_t n=fread(buf,1,sizeof(buf),f); fclose(f); if(strstr(buf,"key")){return 1;} (void)n; } return 0; }
