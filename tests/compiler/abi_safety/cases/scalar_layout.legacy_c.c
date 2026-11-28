#include <stdio.h>

union Uni { long l; double d; char c[8]; };
enum En { EN_ZERO = 0, EN_SEVEN = 7, EN_NINE = 9 };
int main(void){
  printf("c=%zu s=%zu i=%zu l=%zu ll=%zu f=%zu d=%zu\n",
         sizeof(char), sizeof(short), sizeof(int), sizeof(long),
         sizeof(long long), sizeof(float), sizeof(double));
  printf("union=%zu enum=%zu\n",
         sizeof(union Uni), sizeof(enum En));
  return 0;
}
