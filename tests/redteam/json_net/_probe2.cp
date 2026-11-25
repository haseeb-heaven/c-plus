#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/http.hp"
#include "stdlib/net.hp"

int main(void) {
    error e = error();
    /* sink one-over */
    char buf[8];
    http_sink s;
    size_t r;
    memset(buf, 0xA5, sizeof(buf));
    s.buf=buf; s.size=8; s.used=0; s.overflow=0;
    r = http_sink_write("ABCDEFGH", 1, 8, &s); /* fills 7 + NUL */
    printf("sink_exact ret=%zu used=%zu ovf=%d buf[7]=%d\n", r, s.used, s.overflow, (int)(unsigned char)buf[7]);
    s.buf=buf; s.size=8; s.used=0; s.overflow=0;
    memset(buf,0xA5,sizeof(buf));
    r = http_sink_write("ABCDEFGHI", 1, 9, &s); /* one over */
    printf("sink_over ret=%zu used=%zu ovf=%d buf[7]=%d\n", r, s.used, s.overflow, (int)(unsigned char)buf[7]);

    /* CRLF injected header via add */
    http_headers hh = http_headers();
    error_reset(&e);
    int a = hh.add("X: ok\r\nInjected: yes", &e);
    printf("hdr_add rc=%d valid=%d data=%s ok=%d\n", a, hh.valid(),
           hh.native() && hh.native()->data ? hh.native()->data : "(null)", e.ok());
    hh.drop();

    /* connection invalid fd */
    connection c = connection_borrow(-1);
    error_reset(&e);
    int n = c.read(buf, 8, &e);
    printf("conn_read rc=%d fail=%d cat=%d\n", n, e.failed_now(), e.category());
    return 0;
}
