int popcount(unsigned x){ int c=0; while(x){ c+=x&1u; x>>=1; } return c; }
int main(void){ return popcount(0xF0F0)==8 ? 0 : 1; }
