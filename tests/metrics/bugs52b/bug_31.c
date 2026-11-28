#include <stdlib.h>
#include <string.h>
int bad_cmp(const void*a,const void*b){ return strcmp((const char*)a,(const char*)b); }
int main(void){ const char*arr[]={"banana","apple","cherry"}; qsort(arr,3,sizeof(const char*),bad_cmp); return 0; }
