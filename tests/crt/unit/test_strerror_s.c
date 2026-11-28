#include <assert.h>
#include <string.h>
#include <errno.h>
#include "safe_string.h"
int main(void){
    char b[64];
    for(int i=0;i<10;i++){ assert(cplus_strerror_s(b,sizeof(b),EINVAL)==0); assert(strlen(b)>0); }
    assert(cplus_strerror_s(NULL,sizeof(b),EINVAL)!=0);
    assert(cplus_strerror_s(b,0,EINVAL)!=0);
    char small[2]; assert(cplus_strerror_s(small,sizeof(small),EINVAL)!=0); /* dmax too small */
    return 0;
}
