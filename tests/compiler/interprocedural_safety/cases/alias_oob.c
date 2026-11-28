static void wr(int *p){ int *q=p; q[6]=1; }
int main(void){ int a[4]={0}; wr(a); return a[0]; }
