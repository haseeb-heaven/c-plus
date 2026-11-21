#include <assert.h>
int cplus_isascii(int); int cplus_toascii(int);
int main(void){
    for(int c=0;c<128;c++) assert(cplus_isascii(c)==1);
    for(int c=128;c<256;c++) assert(cplus_isascii(c)==0);
    for(int c=0;c<10;c++){ assert(cplus_isascii('A')==1); assert(cplus_toascii(0xFF)==0x7F); assert(cplus_toascii('a')=='a'); }
    return 0;
}
