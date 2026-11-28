static int *getnull(void){ return 0; }
int main(void){ int *p=getnull(); return *p; }
