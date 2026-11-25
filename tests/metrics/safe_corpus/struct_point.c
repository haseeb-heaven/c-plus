struct point { int x,y; };
int main(void){ struct point p={3,4}; int d2=p.x*p.x+p.y*p.y; return d2==25 ? 0 : 1; }
