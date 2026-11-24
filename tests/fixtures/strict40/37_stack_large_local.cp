int main(void) { volatile char large[100000000]; large[0] = 1; return large[0]; }
