#include <assert.h>
#include <string.h>
#include <signal.h>
char *cplus_strsignal(int); int cplus_strcoll(const char*,const char*); size_t cplus_strxfrm(char*,const char*,size_t);
int main(void){
    char *s=cplus_strsignal(SIGINT); assert(s && s[0]);
    for(int i=0;i<10;i++) assert(cplus_strsignal(SIGTERM)!=NULL);
    assert(cplus_strcoll("abc","abc")==0);
    char buf[32]; size_t n=cplus_strxfrm(buf,"hello",sizeof buf); assert(n>0 && strcmp(buf,"hello")==0);
    for(int i=0;i<10;i++){ char b[16]; assert(cplus_strxfrm(b,"xyz",sizeof b)==3); }
    return 0;
}
