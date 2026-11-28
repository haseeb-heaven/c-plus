static void g(int *p,int i);
static void f(int *p,int i){ if(i>5)return; p[i]=1; g(p,i+1); }
static void g(int *p,int i){ f(p,i); }
int main(void){ int a[4]={0}; f(a,0); return a[0]; }
