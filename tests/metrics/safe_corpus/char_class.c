int main(void){ int letters=0; const char *s="a1b2c3";
  for(int i=0;s[i];i++) if((s[i]>='a'&&s[i]<='z')) letters++; return letters==3?0:1; }
