#include "stdlib/vector.hp"
int main(void){ IntVec a=IntVec(); long sum=0; for(int i=0;i<1025;i++)a.push(i*i%101); for(int i=0;i<1025;i++){ if(a.at(i)!=(i*i%101))return 11; sum+=a.at(i); } print("VECTOR",a.len(),sum,a.at(1024),a.at(999999)); return 0; }
