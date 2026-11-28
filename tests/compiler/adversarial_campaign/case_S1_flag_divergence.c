
#include <stdlib.h>
static void consume(int *p){ free(p); }
int main(void){
  int *p = malloc(sizeof(int));
#ifdef DANGER
  consume(p); consume(p);
#else
  free(p);
#endif
  return 0;
}
