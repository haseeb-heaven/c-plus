#include "stdlib/thread.hp"
int main(void){Mutex mu=Mutex_new(); for(int i=0;i<100;i++){Guard g=Guard_lock(&mu); if(i==99)print("GUARD",i);}return 0;}
