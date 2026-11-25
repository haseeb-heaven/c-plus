#include <stdio.h>
int main(void){ FILE *f=fopen("/tmp/cplus_corpus_test.txt","w+"); if(!f) return 2;
  fputs("abc",f); rewind(f); char b[8]={0}; if(!fgets(b,sizeof b,f)){fclose(f);return 2;}
  fclose(f); return b[0]=='a' && b[2]=='c' ? 0 : 1; }
