/* Evidence probe: curl URL parse results, slist behaviour, socketpair reads. */
#include <curl/curl.h>
#include <curl/urlapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/http.hp"
#include "stdlib/net.hp"

static void try_url(const char *s, size_t n) {
    CURLU *u = curl_url();
    CURLUcode rc = curl_url_set(u, CURLUPART_URL, s, 0);
    printf("URL len=%lu rc=%d (%s) :: %.40s\n", (unsigned long)n, (int)rc,
           curl_url_strerror(rc), s);
    curl_url_cleanup(u);
}

int main(void) {
    static const char *bad[] = {
        "", "://", "http://", "http:// ", " http://example.com/",
        "ht tp://example.com/", "http://exa mple.com/", "http://[::1",
        "http://[bad]/", "http://example.com:abc/", "http://example.com:99999/",
        "http://user@:80/", "http://?x=1", "//example.com/path",
        "ftp:/single-slash", "http:///path", "gopher://example.com/",
        "http://example.com/%zz", "http://example.com/#frag\n", "http://\r\n",
        "mailto:user", "file:///no/such/file", (char *)0
    };
    char *longs[8];
    size_t lens[8] = {4096, 8192, 16384, 32768, 65535, 65536, 65537, 200000};
    int i;
    FILE *fp;
    http_headers hh = http_headers();
    error e = error();
    int sp[2];
    char src[128], dst[128];
    connection cr;

    for (i = 0; bad[i] != (char *)0; i++) try_url(bad[i], strlen(bad[i]));
    for (i = 0; i < 8; i++) {
        size_t k = lens[i];
        longs[i] = (char *)malloc(k + 1);
        if (longs[i] == (char *)0) return 2;
        memcpy(longs[i], "http://a/", 9);
        memset(longs[i] + 9, 'a', k - 9);
        longs[i][k] = '\0';
        try_url(longs[i], k);
    }

    printf("SLIST null rc=%d\n", hh.add((const char *)0, &e));
    printf("SLIST empty rc=%d valid=%d\n", hh.add("", &e), hh.valid());
    printf("SLIST crlf rc=%d data=[%s]\n",
           hh.add("X: ok\r\nX-Injected: yes", &e), hh.native()->data);
    hh.drop();

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0) {
        memset(src, 'Z', sizeof(src));
        (void)!write(sp[1], src, 100);
        cr = connection_borrow(sp[0]);
        memset(dst, 0xA5, sizeof(dst));
        printf("SPREAD cap=64 n=%d tail=%d\n", cr.read(dst + 8, 64, &e),
               dst[8 + 64] == (char)0xA5);
        close(sp[0]); close(sp[1]);
    }

    fp = fopen("results/probe2.log", "w");
    if (fp != (FILE *)0) { fprintf(fp, "probe2\n"); fclose(fp); }
    for (i = 0; i < 8; i++) free(longs[i]);
    print("PROBE2", "ok");
    return 0;
}
