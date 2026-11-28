#include <stddef.h>
static int add(int a,int b){return a+b;} static int sub(int a,int b){return a-b;}
int main(void){ int (*ops[2])(int,int)={add,sub}; return ops[0](7,3)==10 && ops[1](7,3)==4 ? 0 : 1; }
