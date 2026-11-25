#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/optimizer/cases/raii_scope.c"
#include <stdio.h>
 struct Tag { int id; };
typedef struct Tag Tag;
#line 3 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/optimizer/cases/raii_scope.c"

void Tag_drop(Tag *self){ if(self->id!=0){ printf("drop=%d\n", self->id); self->id=0; } }
Tag Tag_make(int id){ Tag t = {0}; t.id=id; return t; Tag_drop(&t); 
#line 8 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/optimizer/cases/raii_scope.c"
}
int main(void){
  { Tag t = Tag_make(7); (void)t; Tag_drop(&t); 
#line 10 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/optimizer/cases/raii_scope.c"
}
  printf("rs=done\n");
  return 0;
}