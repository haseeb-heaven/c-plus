#include "cplus/cplus.h"
class Ticket {
 int id;
 Ticket(int n){ this->id=n; }
 int get(void){return this->id;}
 void drop(void){ print("DROP",this->id); }
};
int main(void){ Ticket a=Ticket(1); { Ticket b=Ticket(2); Ticket c=Ticket(3); print("INNER",b.get()+c.get()); } print("OUTER",a.get()); return 0; }
