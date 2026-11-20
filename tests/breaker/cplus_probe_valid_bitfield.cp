struct Flags { unsigned ready:1; unsigned mode:3; };
int main(void) { struct Flags f = {1, 5}; return f.ready==1 && f.mode==5 ? 0:1; }
