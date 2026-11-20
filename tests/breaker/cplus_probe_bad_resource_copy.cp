resource struct T { int x; };
void T_drop(T *t) { t->x=0; }
int main(void) { T a={1}; T b=a; return b.x; }
