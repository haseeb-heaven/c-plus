#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/optimizer/cases/raii_early_return.c"
#include <stdio.h>
 struct Tag { int id; };
typedef struct Tag Tag;
#line 3 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/optimizer/cases/raii_early_return.c"

void Tag_drop(Tag *self){ if(self->id!=0){ printf("drop=%d\n", self->id); self->id=0; } }
Tag Tag_make(int id){ Tag t = {0}; t.id=id; return t; Tag_drop(&t); 
#line 8 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/optimizer/cases/raii_early_return.c"
}
static int early(int id){ Tag t = Tag_make(id); if(id==2) { __typeof__(- 1) __cplus_ret = (- 1); Tag_drop(&t);  return __cplus_ret;; } { __typeof__(id) __cplus_ret = (id); Tag_drop(&t);  return __cplus_ret;; } Tag_drop(&t); 
#line 9 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/optimizer/cases/raii_early_return.c"
}
int main(void){ printf("re=%d\n", early(2)); return 0; }