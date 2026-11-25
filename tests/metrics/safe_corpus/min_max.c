int main(void){ int a[6]={9,2,7,1,8,3}; int lo=a[0],hi=a[0];
  for(int i=1;i<6;i++){ if(a[i]<lo) lo=a[i]; if(a[i]>hi) hi=a[i]; }
  return lo==1&&hi==9?0:1; }
