struct B{ int a[4]; };
static void wr(struct B *b){ b->a[7]=1; }
int main(void){ struct B b; wr(&b); return b.a[0]; }
