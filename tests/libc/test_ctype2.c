#include <assert.h>
#include <stdio.h> /* EOF */
int cplus_isblank(int);
int main(void){
    for(int i=0;i<10;i++){ assert(cplus_isblank(' ')); assert(cplus_isblank('\t')); assert(!cplus_isblank('a')); }
    /* Regression: negative signed char and EOF must not index a ctype table. */
    assert(!cplus_isblank(EOF));
    assert(!cplus_isblank((int)(char)0x80));
    assert(!cplus_isblank((int)(char)0xFF));
    return 0;
}
