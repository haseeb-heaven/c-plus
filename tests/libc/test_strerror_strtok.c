#include <assert.h>
#include <string.h>
#include <errno.h>
char *cplus_strerror(int); char *cplus_strtok(char*,const char*);
int main(void){
    for(int i=0;i<10;i++){ char *e=cplus_strerror(EINVAL); assert(e && e[0]); }
    for(int i=0;i<10;i++){ char buf[]="a,b,c"; char *t=cplus_strtok(buf,","); assert(t && strcmp(t,"a")==0); t=cplus_strtok(NULL,","); assert(t && strcmp(t,"b")==0); t=cplus_strtok(NULL,","); assert(t && strcmp(t,"c")==0); }
    return 0;
}
