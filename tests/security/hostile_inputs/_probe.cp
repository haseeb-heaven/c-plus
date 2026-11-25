/* Temporary strict-mode compatibility probe (removed after use). */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"
#include "stdlib/io.hp"
#include "stdlib/json.hp"
#include "stdlib/fs.hp"
#include "stdlib/net.hp"

static unsigned char probe_byte(const char *p, size_t idx) {
    unsigned char b;
    __builtin_memcpy(&b, p + idx, 1);
    return b;
}

int main(int argc, char **argv) {
    error err = error();
    char buf[32];
    int rc;
    if (argc > 1) {
        FILE *swapped = freopen(argv[1], "r", stdin);
        if (swapped != NULL) {
            print("probe_freopen", 1);
        }
    }
    rc = read_line(buf, sizeof(buf), &err);
    print("probe_read_line_rc", rc);
    print("probe_errcat", err.category());
    print("probe_byte", (int)probe_byte(buf, 0));
    {
        json doc = json("{\"a\":1}", &err);
        print("probe_json_valid", doc.valid());
        doc.drop();
    }
    {
        file f = file("tests/security/hostile_inputs/NOTES.md", "rb");
        print("probe_file_open", f.is_open());
        f.drop();
    }
    print("probe_fs_exists", fs_exists("."));
    {
        connection c = connection_borrow(-1);
        print("probe_conn_invalid", c.is_valid() == 0);
    }
    return 0;
}
