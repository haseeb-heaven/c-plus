#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <signal.h>
#include <math.h>
#include <time.h>
#include <locale.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../libc/install-include/cplus_libc.h"
int main(void){
    assert(CHAR_BIT>=8);
    void *p=cplus_malloc(1); assert(p); cplus_free(p);
    return 0;
}
