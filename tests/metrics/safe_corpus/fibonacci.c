int main(void){ int a=0,b=1; for(int i=0;i<10;i++){int t=a+b;a=b;b=t;} return a==55 ? 0 : 1; }
