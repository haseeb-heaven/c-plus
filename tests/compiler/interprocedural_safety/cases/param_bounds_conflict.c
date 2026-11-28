static void wr(int *p){ p[2]=1; }
int main(void){ int a[4]={0}; int b[8]={0}; wr(a); wr(b); return a[2]; }
