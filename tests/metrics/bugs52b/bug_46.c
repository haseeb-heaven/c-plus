#include <stdlib.h>
#include <string.h>
struct Item { int* data; };
int main(void){ struct Item*arr=(struct Item*)malloc(3*sizeof(struct Item)); arr[0].data=(int*)malloc(sizeof(int)); arr[1].data=(int*)malloc(sizeof(int)); arr[2].data=(int*)malloc(sizeof(int)); free(arr[0].data); memmove(&arr[0],&arr[1],2*sizeof(struct Item)); for(int i=0;i<3;i++){free(arr[i].data);} free(arr); return 0; }
