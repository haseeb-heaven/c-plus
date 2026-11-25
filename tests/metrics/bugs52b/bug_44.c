#include <stdlib.h>
struct Entry { char* name; };
int main(void){ struct Entry*list=(struct Entry*)malloc(5*sizeof(struct Entry)); for(int i=0;i<5;i++){ if(i==3){ for(int j=0;j<5;j++){free(list[j].name);} free(list); return 1;} list[i].name=(char*)malloc(16);} return 0; }
