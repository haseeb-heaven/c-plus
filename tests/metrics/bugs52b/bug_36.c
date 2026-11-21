#include <stdlib.h>
void consume_and_free(int**p){ free(*p); }
int main(void){ int*buf=(int*)malloc(sizeof(int)); *buf=7; consume_and_free(&buf); if(buf!=NULL){*buf=8;} return 0; }
