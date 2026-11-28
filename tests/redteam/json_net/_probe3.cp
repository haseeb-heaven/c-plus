#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/http.hp"

#define GSZ 144
static char g[GSZ];
int main(void) {
    int i;
    for (i=0;i<GSZ;i++) g[i]=(char)0xA5;
    char *buf = g + 16;
    http_sink s;
    char src[8];
    memset(src,'B',sizeof(src));
    s.buf=buf; s.size=64; s.used=SIZE_MAX; s.overflow=0;
    size_t r = http_sink_write(src,1,8,&s);
    printf("ret=%zu overflow=%d\n", r, s.overflow);
    printf("guard16 (expect all A5):\n");
    for (i=8;i<16;i++) printf(" g[%d]=%02x", i, (unsigned char)g[i]);
    printf("\nbuf first 12 (expect B then A5):\n");
    for (i=0;i<12;i++) printf(" buf[%d]=%02x", i, (unsigned char)buf[i]);
    printf("\n");
    return 0;
}
