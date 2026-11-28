struct inner { int v; }; struct outer { struct inner in; int w; };
int main(void){ struct outer o={{3},4}; return o.in.v+o.w==7?0:1; }
