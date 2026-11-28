/* Exact reproducer for HTTP header CRLF-injection finding. */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/http.hp"

int main(void) {
    error e = error();
    http_headers hh = http_headers();
    const char *line = "X-Adversarial: ok\r\nX-Injected: yes";
    int rc;
    error_reset(&e);
    rc = hh.add(line, &e);
    printf("rc=%d valid=%d ok=%d native=%p\n", rc, hh.valid(), e.ok(),
           (void *)hh.native());
    printf("stored_bytes:");
    if (hh.native() && hh.native()->data) {
        const unsigned char *p = (const unsigned char *)hh.native()->data;
        size_t i, n = strlen((const char *)p);
        for (i = 0; i < n; i++) printf(" %02x", p[i]);
        printf("\nstrcmp_identical=%d\n", strcmp(hh.native()->data, line) == 0);
    } else {
        printf(" (null)\nstrcmp_identical=0\n");
    }
    hh.drop();
    return 0;
}
