#include <stdio.h>

int abi_glob_counter = 41;
int abi_add_exported(int a,int b){ int t=a+b; return t; }
static int abi_add_hidden(int a,int b){ int t=a-b; return t; }
int main(void){
  abi_glob_counter = 42;
  printf("sym=%d,%d,%d\n",
         abi_add_exported(3,4), abi_add_hidden(9,2), abi_glob_counter);
  return 0;
}
