#include "cplus/cplus.h"
int main(void) { managed int *p = cplus_new_array(int, SIZE_MAX / 2); (void)p; return 0; }
