#include "stdlib/string.hp"
int main(void){ long total=0; for(int i=0;i<200;i++){ string a=string("0123456789"); if(a.size()!=10)return 10; total+=a.at((size_t)(i%10))-'0'; } print("STRING",total);return 0; }
