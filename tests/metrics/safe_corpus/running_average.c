int main(void){ int a[5]={2,4,6,8,10}; int s=0; double avg=0;
  for(int i=0;i<5;i++){ s+=a[i]; avg=(double)s/(i+1); }
  return avg==6.0?0:1; }
