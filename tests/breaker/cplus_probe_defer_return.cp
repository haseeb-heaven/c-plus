static int value=0;
int operation(void) { defer value = value+7; return 3; }
int main(void) { int n=operation(); return n==3 && value==7 ? 0 : 1; }
