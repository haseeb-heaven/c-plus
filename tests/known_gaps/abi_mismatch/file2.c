/* NOT DETECTED cross-TU: declaration as double. */
#include <stdio.h>
extern double shared_value;
int main(void) { printf("%f\n", shared_value); return 0; }
