/* Combined consumer: all nine standard C headers in one C++17 TU. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <assert.h>
int stdc_all_std_probe()
{
    char buf[16];
    size_t n = strlen("ok");
    memset(buf, 0, sizeof buf);
    assert(n == 2);
    return (int)n + isalpha('x') + (INT_MAX > 0) + (errno == 0) + (int)(uint32_t)1;
}
