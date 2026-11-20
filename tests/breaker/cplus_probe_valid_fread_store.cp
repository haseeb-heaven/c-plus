#include <stdio.h>
int main(void) {
 FILE *f=tmpfile(); if(!f) return 0;
 char buf[8]={0}; size_t n=fread(buf,1,sizeof buf,f);
 if(ferror(f)) { fclose(f); return 1; }
 fclose(f); return n==0 ? 0 : 2;
}
