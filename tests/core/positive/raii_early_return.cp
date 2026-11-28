#include "cplus/cplus.h"
static int drops=0;
resource struct T{int id;};
void T_drop(T*t){if(t->id){drops++;t->id=0;}}
static int f(int x){T t={1};if(x)return 7;return 9;}
int main(void){return f(1)==7&&drops==1?0:1;}
