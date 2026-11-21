#include <assert.h>
int cplus_isblank(int);
int main(void){
    for(int i=0;i<10;i++){ assert(cplus_isblank(' ')); assert(cplus_isblank('\t')); assert(!cplus_isblank('a')); }
    return 0;
}
