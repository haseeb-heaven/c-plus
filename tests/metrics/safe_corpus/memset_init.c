#include <string.h>
int main(void){ int a[16]; memset(a,0,sizeof a); int s=0; for(int i=0;i<16;i++) s+=a[i]; return s==0?0:1; }
