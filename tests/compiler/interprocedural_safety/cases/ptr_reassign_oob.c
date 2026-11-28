static void wr(int *p){ int dummy[2]={0}; p=dummy; p[5]=1; }
int main(void){ int a[4]={0}; wr(a); return a[0]; }
