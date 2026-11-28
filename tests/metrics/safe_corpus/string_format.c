#include <stdio.h>
int main(void){ char buf[32]; int n=snprintf(buf,sizeof buf,"%d-%s",7,"ok");
  return n>0 && buf[0]=='7' ? 0 : 1; }
