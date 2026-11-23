static void h(int *p){ p[6]=1; }
static void g(int *p){ h(p); }
static void f(int *p){ g(p); }
int main(void){ int a[4]={0}; f(a); return a[0]; }
