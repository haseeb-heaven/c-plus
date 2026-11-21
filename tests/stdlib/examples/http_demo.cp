/* Demo: libcurl GET over file:// and data:, with headers and errors. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/http.hp"

int main(void) {
    error err = error();
    http global = http(&err);
    if (!global.valid()) {
        print("curl failed", err.what());
        return 1;
    }
    print("curl", http_version() != NULL);

    http_headers headers = http_headers();
    headers.add("X-Demo: cplus", &err);
    print("headers", headers.valid());

    http_client client = http_client(&err);
    if (!client.valid())
        return 2;
    client.set_timeout(5, &err);
    client.follow_redirects(1, &err);
    client.set_headers(&headers, &err);

    char body[1024];
    if (client.get("file://tests/stdlib/data/sample.json", body, sizeof(body),
                   &err) == STDLIB_OK)
        print("file body", body, client.bytes());

    if (client.get("data:,inline-payload", body, sizeof(body), &err) == STDLIB_OK)
        print("data body", body, client.status());

    int refused = client.get("http://127.0.0.1:1/nope", body, sizeof(body), &err);
    print("error path", refused == STDLIB_FAIL, err.failed_now());
    return 0;
}
