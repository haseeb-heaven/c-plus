static int deref(int *p){ return *p; }
int main(void){ int *q=0; return deref(q); }
