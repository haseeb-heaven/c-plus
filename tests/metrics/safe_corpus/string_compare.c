int main(void) { const char *a="abc", *b="abd"; int i=0;
  while (a[i] && a[i]==b[i]) i++; return a[i] < b[i] ? 0 : 1; }
