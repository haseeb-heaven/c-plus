static void swap(int *a,int *b){ int t=*a; *a=*b; *b=t; }
int main(void){ int x=1,y=2; swap(&x,&y); return x==2 && y==1 ? 0 : 1; }
