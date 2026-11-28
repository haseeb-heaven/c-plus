#include <stdarg.h>
#include <stdio.h>
void print_vals(int count,...){ va_list args; va_start(args,count); for(int i=0;i<count;i++){ double d=va_arg(args,double); printf("%f\n",d);} va_end(args);}
int main(void){ print_vals(1,42); return 0; }
