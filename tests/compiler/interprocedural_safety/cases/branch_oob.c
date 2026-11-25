static void wr(int *p,int c){ if(c) p[8]=1; else p[0]=2; }
int main(void){ int a[4]={0}; wr(a,1); return a[0]; }
