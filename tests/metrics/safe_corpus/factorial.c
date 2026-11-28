unsigned long long fact(int n){ unsigned long long r=1; for(int i=2;i<=n;i++) r*=i; return r; }
int main(void){ return fact(10)==3628800ULL ? 0 : 1; }
