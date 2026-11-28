#include <assert.h>
#include <stdio.h>
int cplus_isalpha(int); int cplus_isdigit(int); int cplus_isspace(int);
int main(void){
    assert(cplus_isalpha('A')); assert(!cplus_isalpha('1'));
    assert(cplus_isdigit('9')); assert(!cplus_isdigit('a'));
    assert(cplus_isspace(' ')); assert(cplus_isspace('\n')); assert(!cplus_isspace('x'));
    puts("ctype: ok"); return 0;
}
