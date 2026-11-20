#include <stdio.h>
int main(void){ int a[3][4][5]={0}; long sum=0; for(int i=0;i<60;i++) a[i/20][(i/5)%4][i%5]=i; for(int x=0;x<3;x++)for(int y=0;y<4;y++)for(int z=0;z<5;z++)sum+=a[x][y][z]; printf("THREED %ld\n",sum); return sum==1770?0:5; }
