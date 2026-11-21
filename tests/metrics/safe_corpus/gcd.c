int gcd(int a,int b){ while(b){int t=a%b;a=b;b=t;} return a; }
int main(void){ return gcd(48,18)==6 ? 0 : 1; }
