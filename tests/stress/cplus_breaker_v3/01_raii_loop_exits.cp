#include "cplus/cplus.h"
resource struct R { int id; };
void R_drop(R *self) { if(self->id) { print("DROP",self->id); self->id=0; } }
int main(void) {
    for(int i=1; i<=4; ++i) {
        R item={i};
        if(i==1) continue;
        if(i==3) break;
        print("STEP",i);
    }
    print("END"); return 0;
}
