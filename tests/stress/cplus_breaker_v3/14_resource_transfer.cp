#include "cplus/cplus.h"
resource struct Token {int id;};
void Token_drop(Token *self) {if(self->id){print("DROP",self->id);self->id=0;}}
Token make(int x){Token a={x};return a;}
int main(void){Token a=make(7);Token b=cplus_move(a);print("STATE",a.id,b.id);return 0;}
