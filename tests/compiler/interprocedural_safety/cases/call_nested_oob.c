static void g(int *p){ p[5]=1; }
static void f(int *p){ g(p); }
int main(void){ int a[4]={0}; f(a); return a[0]; }
