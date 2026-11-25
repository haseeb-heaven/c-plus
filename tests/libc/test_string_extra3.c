#include <assert.h>
#include <string.h>
#include <stddef.h>

size_t cplus_strspn(const char*,const char*);
size_t cplus_strcspn(const char*,const char*);
char *cplus_strpbrk(const char*,const char*);

int main(void){
    for(int i=0;i<10;i++){
        assert(cplus_strspn("abc123","abc")==3);
        assert(cplus_strcspn("abc123","123")==3);
        assert(cplus_strpbrk("abc123","12")== (char*)strchr("abc123",'1') || cplus_strpbrk("abc123","12")!=NULL);
        assert(cplus_strspn("", "a")==0);
        assert(cplus_strcspn("", "a")==0);
        assert(cplus_strpbrk("abc","z")==NULL);
    }
    return 0;
}
