#include <stdio.h>
resource struct Tag { int id; };
void Tag_drop(Tag *self){ if(self->id!=0){ printf("drop=%d\n", self->id); self->id=0; } }
Tag Tag_make(int id){ Tag t; t.id=id; return t; }
int main(void){
  { Tag t = Tag_make(7); (void)t; }
  printf("rs=done\n");
  return 0;
}
