#include <stdio.h>
#include <stddef.h>

#pragma pack(push, 1)
struct Packed { char a; int b; double c; };
#pragma pack(pop)
struct Inner { char x; int y; };
struct Outer { char a; struct Inner in; short b; };
struct Arr { char c; int v[3]; double d; };
int main(void){
  printf("packed=%zu,%zu,%zu,%zu\n",
         sizeof(struct Packed), offsetof(struct Packed, a),
         offsetof(struct Packed, b), offsetof(struct Packed, c));
  printf("outer=%zu,%zu,%zu,%zu,%zu,%zu\n",
         sizeof(struct Outer), offsetof(struct Outer, a),
         offsetof(struct Outer, in), offsetof(struct Outer, b),
         offsetof(struct Outer, in.x), offsetof(struct Outer, in.y));
  printf("arr=%zu,%zu,%zu,%zu\n",
         sizeof(struct Arr), offsetof(struct Arr, c),
         offsetof(struct Arr, v), offsetof(struct Arr, d));
  return 0;
}
