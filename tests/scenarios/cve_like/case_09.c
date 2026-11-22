/* Original user-supplied crash scenario 9 (minimally adjusted to compile). */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
static uint64_t process_window(size_t n){ uint64_t scratch[n]; scratch[0]=1; scratch[n-1]=2; return scratch[0]+scratch[n-1]; }
int main(void){ size_t len=0; printf("len: "); if(scanf("%zu",&len)!=1||len==0) return 1;
 printf("res: %llu\n",(unsigned long long)process_window(len)); return 0; }
