#include <assert.h>
#include <stdio.h>
int cplus_isalpha(int); int cplus_isdigit(int); int cplus_isspace(int); int cplus_isalnum(int);
int cplus_isupper(int); int cplus_islower(int); int cplus_isxdigit(int); int cplus_isprint(int);
int cplus_isgraph(int); int cplus_ispunct(int); int cplus_iscntrl(int); int cplus_toupper(int); int cplus_tolower(int);
int main(void){
    assert(cplus_isalnum('Z') && cplus_isalnum('5') && !cplus_isalnum(' '));
    assert(cplus_isupper('A') && !cplus_isupper('a'));
    assert(cplus_islower('a') && !cplus_islower('A'));
    assert(cplus_isxdigit('f') && cplus_isxdigit('9') && !cplus_isxdigit('g'));
    assert(cplus_isprint(' ') && !cplus_iscntrl(' '));
    assert(cplus_isgraph('x') && !cplus_isgraph(' '));
    assert(cplus_ispunct('!') && !cplus_ispunct('a'));
    assert(cplus_iscntrl('\n'));
    assert(cplus_toupper('a')=='A');
    assert(cplus_tolower('Z')=='z');
    puts("ctype_full: ok");
    return 0;
}
