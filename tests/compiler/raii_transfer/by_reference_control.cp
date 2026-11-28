#include <stdio.h>
#include <stdlib.h>
class Resource {
  int *ptr;
  Resource(void){ this->ptr=(int*)malloc(sizeof(int)); if(this->ptr)*this->ptr=42; }
  void drop(void){ if(this->ptr){ printf("DROP\n"); free(this->ptr); this->ptr=0; } }
};
static void peek(Resource *r){ printf("PEEK %d\n", *r->ptr); }
int main(void){ Resource r = Resource(); peek(&r); return 0; }
