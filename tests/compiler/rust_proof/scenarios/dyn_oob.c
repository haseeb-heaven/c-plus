#include <stdlib.h>
int main(void){int a[4]={0};const char*s=getenv("PROOF_IDX");
               int i=s?atoi(s):100;return a[i];}
