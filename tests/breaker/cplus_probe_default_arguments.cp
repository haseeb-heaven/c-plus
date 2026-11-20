int add(int a, int b = 2, int c = 3) { return a+b+c; }
int main(void) { return add(1)==6 && add(1,4)==8 && add(1,4,5)==10 ? 0 : 1; }
