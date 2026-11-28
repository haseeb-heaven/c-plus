#include <stdlib.h>
int main(void){ size_t cap=8; int*ring=(int*)malloc(cap*sizeof(int)); size_t head=7; head=(head+1)%(cap+1); ring[head]=99; free(ring); return 0; }
