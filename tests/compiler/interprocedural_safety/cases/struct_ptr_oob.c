struct B{ int *p; };
static void wr(struct B *b){ b->p[6]=1; }
int main(void){ int a[4]={0}; struct B b; b.p=a; wr(&b); return a[0]; }
