/* Evidence probe 3: URL length limits + header line copy content. */
#include <curl/curl.h>
#include <curl/urlapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/http.hp"

int main(void) {
    size_t lens[8] = {1000000, 4000000, 7000000, 8000000, 8000001,
                      8000100, 16000000, 32000000};
    char *big = (char *)malloc(32000000u + 2u);
    CURLU *u;
    CURLUcode rc;
    int i;
    http_headers hh = http_headers();
    error e = error();
    char *p;
    const char *line;

    if (big == (char *)0) return 2;
    for (i = 0; i < 8; i++) {
        size_t k = lens[i];
        memcpy(big, "http://a/", 9);
        memset(big + 9, 'a', k - 9);
        big[k] = '\0';
        u = curl_url();
        rc = curl_url_set(u, CURLUPART_URL, big, 0);
        printf("LONG len=%lu rc=%d (%s)\n", (unsigned long)k, (int)rc,
               curl_url_strerror(rc));
        curl_url_cleanup(u);
    }
    free(big);

    line = "X: ok\r\nX-Injected: yes";
    printf("ADD rc=%d\n", hh.add(line, &e));
    p = hh.native()->data;
    printf("HDR p=[%s] len=%lu eq=%d\n", p, (unsigned long)strlen(p),
           strcmp(p, line) == 0);
    error_reset(&e);
    printf("ADD2 rc=%d\n", hh.add("X-Second: 2", &e));
    p = hh.native()->next->data;
    printf("HDR2 p=[%s] eq=%d\n", p, strcmp(p, "X-Second: 2") == 0);
    hh.drop();
    print("PROBE3", "ok");
    return 0;
}
