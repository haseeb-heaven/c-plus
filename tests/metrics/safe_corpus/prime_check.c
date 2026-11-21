int is_prime(int n){ if(n<2) return 0; for(int d=2;d*d<=n;d++) if(n%d==0) return 0; return 1; }
int main(void){ int c=0; for(int i=2;i<30;i++) if(is_prime(i)) c++; return c==10 ? 0 : 1; }
