#include <stdio.h>
int main(void){ FILE *f=tmpfile(); if(!f)return 2; const char txt[]="ABCD"; size_t written=fwrite(txt,1,4,f); if(written!=4){fclose(f);return 3;} rewind(f); char b[8]={0}; size_t got=fread(b,1,sizeof b,f); int bad=ferror(f); fclose(f); printf("IO %zu %d %c\n",got,bad,b[3]); return got==4 && !bad && b[3]=='D'?0:4; }
