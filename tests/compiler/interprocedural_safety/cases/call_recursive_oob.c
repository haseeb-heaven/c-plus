static void rec(int *p,int i){ if(i>5) return; p[i]=1; rec(p,i+1); }
int main(void){ int a[4]={0}; rec(a,0); return a[0]; }
