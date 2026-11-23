static void wr(int *p){ p[8]=9; }
int main(void){ int a[4]={0}; wr(a); return a[0]; }
