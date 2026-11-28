#include <stdio.h>
#include <stdlib.h>
class Resource {
  int *ptr;
  Resource(void){ this->ptr=(int*)malloc(sizeof(int)); if(this->ptr)*this->ptr=42; }
  void drop(void){ if(this->ptr){ printf("DROP\n"); free(this->ptr); this->ptr=0; } }
};
static void consume(Resource r){ printf("CONSUME %d\n", *r.ptr); }
int main(void){ Resource r = Resource(); consume(cplus_move(r)); return 0; }
