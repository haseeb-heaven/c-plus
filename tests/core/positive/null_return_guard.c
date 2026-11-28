static int *bad(void){ return 0; }
int main(void){ int *p = bad(); if(p) return *p; return 0; }
