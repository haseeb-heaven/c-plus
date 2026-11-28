#include <assert.h>
#include <stdlib.h>
#include <string.h>
char *cplus_strndup(const char*, size_t);
void cplus_free(void*);
int main(void){
    for(int i=0;i<10;i++){
        char *d=cplus_strndup("hello",3); assert(d && strcmp(d,"hel")==0); free(d);
        char *e=cplus_strndup("hi",10); assert(e && strcmp(e,"hi")==0); free(e);
        char *n=cplus_strndup(NULL,5); assert(n==NULL);
    }
    return 0;
}
