
static int zero(void){ return 0; }
int f(int d){ if(d){ d=zero(); return 100/d; } return 0; }
int main(void){ return f(1); }
