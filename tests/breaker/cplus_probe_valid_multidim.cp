static int g[4][4];
int main(void) { for(int i=0;i<16;i++) g[i/4][i%4]=i; return g[3][3]==15 ? 0 : 1; }
