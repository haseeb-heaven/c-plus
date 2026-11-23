#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/fs.hp"

static int probe_write(const char *path, const char *data, int n) {
    FILE *fp = fopen(path, "wb");
    int ok = 0;
    if (fp != NULL) {
        ok = fwrite(data, 1, (size_t)n, fp) == (size_t)n;
        fclose(fp);
    }
    return ok;
}

int main(void) {
    error e = error();
    char pid[24];
    char a[160], b[160];
    char buf[64];
    FILE *fp;
    int n, rc;
    snprintf(pid, sizeof(pid), "%d", (int)getpid());
    snprintf(a, sizeof(a), "/tmp/cplus_probe_%sa.txt", pid);
    snprintf(b, sizeof(b), "/tmp/cplus_probe_%sb.txt", pid);
    print("wA", probe_write(a, "OLDLONGCONTENT", 14));
    print("wB", probe_write(b, "NEW", 3));
    print("szA", (int)fs_size(a, &e));
    print("szB", (int)fs_size(b, &e));
    rc = fs_rename(a, b, &e);
    print("rcRename", rc);
    print("errOk", e.ok());
    print("existsA", fs_exists(a));
    print("existsB", fs_exists(b));
    print("szB2", (int)fs_size(b, &e));
    memset(buf, 0, sizeof(buf));
    fp = fopen(b, "rb");
    print("fopenB", fp != NULL);
    if (fp != NULL) {
        n = (int)fread(buf, 1, sizeof(buf) - 1, fp);
        print("readN", n);
        fclose(fp);
        print("buf", buf);
    }
    unlink(a);
    unlink(b);
    return 0;
}
