#include "stdlib/thread.hp"
int main(void){mutex mu=mutex(); for(int i=0;i<100;i++){guard g=guard(&mu); if(i==99)print("GUARD",i);}return 0;}
