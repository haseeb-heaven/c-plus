#include <stdio.h>
#include <stddef.h>

struct Mixed { char a; int b; double c; short d; };
int main(void){
  printf("size=%zu a=%zu b=%zu c=%zu d=%zu\n",
         sizeof(struct Mixed), offsetof(struct Mixed, a),
         offsetof(struct Mixed, b), offsetof(struct Mixed, c),
         offsetof(struct Mixed, d));
  return 0;
}
