static void wr(int *p){ int i=0; while(i<7){ p[i]=i; i++; } }
int main(void){ int a[4]={0}; wr(a); return a[0]; }
