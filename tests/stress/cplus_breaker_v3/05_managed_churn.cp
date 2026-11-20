#include "cplus/cplus.h"
int main(void){ unsigned long long sum=0; for(int n=1;n<=500;n++){ managed int *a=cplus_new_array(int, 31); for(int i=0;i<31;i++)a[i]=n+i; if(a.len != 31)return 2; sum+=(unsigned)a[30]; } print("CHURN",sum); return 0; }
