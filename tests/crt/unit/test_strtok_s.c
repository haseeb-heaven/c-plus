#include <assert.h>
#include <string.h>
#include "safe_string.h"
int main(void){
    char buf[32] = "a,b,c";
    rsize_t dmax = 32;
    char *p = NULL;
    char *t = cplus_strtok_s(buf, &dmax, ",", &p);
    assert(t && strcmp(t,"a")==0);
    t = cplus_strtok_s(NULL, &dmax, ",", &p);
    assert(t && strcmp(t,"b")==0);
    t = cplus_strtok_s(NULL, &dmax, ",", &p);
    assert(t && strcmp(t,"c")==0);
    t = cplus_strtok_s(NULL, &dmax, ",", &p);
    assert(t==NULL || strcmp(t,"c")==0);
    return 0;
}
