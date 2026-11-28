int *f(void){int x=1;return &x;}
int main(void){int*p=f();return *p;}
