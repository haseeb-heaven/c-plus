#include "cplus/cplus.h"
class Meter {
 int value;
 Meter(int x){ this->value=x; }
 void inc(int n){ this->value += n; }
 int get(void){ return this->value; }
 int triple(void){ return this->get()*3; }
};
int main(void){ Meter m=Meter(11); for(int i=0;i<10;i++)m.inc(i); print("METER",m.get(),m.triple()); return 0; }
