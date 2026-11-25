static int *mk(void){ int x=7; return &x; }
int main(void){ int *p=mk(); int v=*p; (void)v; return 0; }
