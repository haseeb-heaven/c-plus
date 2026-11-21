#include <stdio.h>
#include <stdlib.h>
int main(void){ setenv("MY_VAR","initial_value",1); char*val=getenv("MY_VAR"); for(int i=0;i<100;i++){setenv("OTHER_VAR","growing_environment_forces_realloc",1);} printf("%s\n",val); return 0; }
