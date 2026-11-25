/* Real web server app (not a demo) on the stdlib/web.hp HTTP/1.1 server.
 *
 * Serves GET /health -> 200 "healthy", GET / -> 200 "index page",
 * anything else -> 404.  Serves up to WEB_SERVE_MAX requests, then exits so
 * a runner can verify deterministically: the runner starts this app, curls a
 * few paths, and diffs stdout against expected/app.txt.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/web.hp"

#define WEB_SERVE_MAX 4

int main(void) {
    web_server srv;   /* stack storage: no heap allocation in this app */
    web_conn c;
    int accepted = 0;
    int i;
    int served = 0;

    if (web_server_init(&srv, (const char *)"127.0.0.1", 18099) != 0) {
        printf("WEB_APP bind_failed\n");
        return 1;
    }
    printf("WEB_APP listening 127.0.0.1:18099\n");

    for (i = 0; i < 200 && served < WEB_SERVE_MAX; i++) {
        accepted = web_accept(&srv, &c);
        if (accepted != 1)
            continue;                      /* nap_ms timeout, try again */
        printf("WEB_APP request path=%s\n", c.req.path);
        if (strcmp(c.req.path, "/health") == 0)
            web_reply(&c, 200, "healthy");
        else if (strcmp(c.req.path, "/") == 0)
            web_reply(&c, 200, "index page");
        else
            web_reply(&c, 404, "not found");
        web_conn_close(&c);
        served++;
    }
    printf("WEB_APP served=%d done\n", served);
    web_server_shutdown(&srv);
    return 0;
}
