int main(void) { int value = 1; int *first = &value; int *second = &value; *first = 2; *second = 3; return value; }
