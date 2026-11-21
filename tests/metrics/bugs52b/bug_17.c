#include <alloca.h>
int main(void){ for(int i=0;i<100000;i++){ char*tmp=(char*)alloca(1024); *tmp='A'; } return 0; }
