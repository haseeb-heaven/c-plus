#include <stdio.h>
resource struct Tag { int id; };
void Tag_drop(Tag *self){ if(self->id!=0){ printf("drop=%d\n", self->id); self->id=0; } }
Tag Tag_make(int id){ Tag t; t.id=id; return t; }
static int early(int id){ Tag t = Tag_make(id); if(id==2) return -1; return id; }
int main(void){ printf("re=%d\n", early(2)); return 0; }
