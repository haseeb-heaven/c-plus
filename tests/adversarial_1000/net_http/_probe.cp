/* Toolchain probe: curl URL API + stdio + socketpair + http_sink_write. */
#include <curl/curl.h>
#include <curl/urlapi.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/http.hp"
#include "stdlib/net.hp"

int main(void) {
    CURLU *u = curl_url();
    CURLUcode rc;
    char *host = (char *)0;
    FILE *fp = fopen("results/probe.log", "w");
    http_sink sink;
    char buf[64];
    int sp[2];
    connection cprobe;
    error eprobe = error();

    if (fp != (FILE *)0) {
        fprintf(fp, "probe rc=%d\n", (int)curl_url_set(u, CURLUPART_URL, "http://[bad", 0));
        fclose(fp);
    }
    rc = curl_url_set(u, CURLUPART_URL, "http://127.0.0.1:9/x", 0);
    if (rc == CURLUE_OK) {
        curl_url_get(u, CURLUPART_HOST, &host, 0);
        if (host != (char *)0) curl_free(host);
    }
    curl_url_cleanup(u);

    memset(buf, 0x41, sizeof(buf));
    sink.buf = buf + 4;
    sink.size = 8;
    sink.used = 0;
    sink.overflow = 0;
    fprintf(stderr, "sink-ret=%lu used=%lu ovf=%d sentinel=%d\n",
            (unsigned long)http_sink_write((char *)"abcdefgh", 1, 8, (void *)&sink),
            (unsigned long)sink.used, sink.overflow, buf[0] == 0x41 && buf[sizeof(buf)-1] == 0x41);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0) {
        (void)!write(sp[1], "hi", 2);
        cprobe = connection_borrow(sp[0]);
        fprintf(stderr, "conn-ret=%d valid=%d\n",
                cprobe.read(buf, sizeof(buf), &eprobe), cprobe.is_valid());
        (void)!close(sp[0]);
        (void)!close(sp[1]);
    }
    print("PROBE", "ok");
    return 0;
}
