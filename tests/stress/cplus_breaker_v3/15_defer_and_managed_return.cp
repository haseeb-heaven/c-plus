#include "cplus/cplus.h"
int compute(int x) { defer print("DEFER",x); managed int *p=cplus_new_array(int,3); p[0]=x; p[1]=x+1; p[2]=x+2; if(x==5)return p[0]+p[1]+p[2]; return 0; }
int main(void){int n=compute(5);print("SUM",n);return n==18?0:2;}
