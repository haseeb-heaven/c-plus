static void wr(int *p){ for(int i=0;i<6;i++) p[i]=i; }
int main(void){ int a[4]={0}; wr(a); return a[0]; }
