
int a[4];
int f(int i){ if(i<=0) return a[0]; return a[i] + f(i-1); }
int main(void){ return f(10); }
