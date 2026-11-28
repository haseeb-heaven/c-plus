static void wr(int **pp){ (*pp)[6]=1; }
int main(void){ int a[4]={0}; int *p=a; wr(&p); return a[0]; }
