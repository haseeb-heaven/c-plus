int main(void){ int total=0; for(int i=0;i<4;i++) for(int j=0;j<4;j++) total += (i==j)?1:0;
  return total==4 ? 0 : 1; }
