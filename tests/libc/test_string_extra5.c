/* Covers the platform-backed string wrappers added in batch 5 (swab). */
#define _DEFAULT_SOURCE
#include <assert.h>
#include <string.h>
#include <sys/types.h>
void cplus_swab(const void*,void*,ssize_t);
int main(void){
    for(int i=0;i<10;i++){
        char src[8]="ABCDEFGH"; char dst[8]={0};
        cplus_swab(src,dst,8);
        assert(dst[0]=='B' && dst[1]=='A' && dst[2]=='D' && dst[3]=='C');
        assert(dst[4]=='F' && dst[5]=='E' && dst[6]=='H' && dst[7]=='G');
        assert(src[0]=='A' && src[1]=='B'); /* source is unmodified */
    }
    { char src[4]="xy"; char dst[4]={0}; cplus_swab(src,dst,2); assert(dst[0]=='y' && dst[1]=='x' && dst[2]==0); }
    for(int i=0;i<10;i++){
        const char s[6]={'a','b','c','d','e','f'}; char d[6]={0};
        cplus_swab(s,d,6);
        assert(d[0]=='b' && d[1]=='a' && d[2]=='d' && d[3]=='c' && d[4]=='f' && d[5]=='e');
    }
    return 0;
}
