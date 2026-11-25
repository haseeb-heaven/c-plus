void f(int*a,int*b){*a=1;*b=2;}
int main(void){int x=0;f(&x,&x);return x;}
