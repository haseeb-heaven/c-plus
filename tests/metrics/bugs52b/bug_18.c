int recurse(int n){ char frame[4096]; frame=(char)n; if(n==0) return frame; return recurse(n+1); }
int main(void){ return recurse(1); }
