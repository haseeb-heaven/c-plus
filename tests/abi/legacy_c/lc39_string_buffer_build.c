/*
 * lc39 - string buffers: manual fixed-capacity builder with offset tracking.
 *
 * Independent legacy-C ABI probe: append via memcpy into a caller-owned char
 * buffer, enforcing capacity and NUL termination at every step.
 */
#include <stdio.h>
#include <string.h>

struct sbuf {
    char data[32];
    size_t len;
};

static int append(struct sbuf *b, const char *text)
{
    size_t n = strlen(text);
    if (b->len + n >= sizeof(b->data)) {
        return 0;
    }
    memcpy(b->data + b->len, text, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 1;
}

int main(void)
{
    struct sbuf b;
    int ok = 1;

    b.data[0] = '\0';
    b.len = 0;
    ok &= append(&b, "lc39");
    ok &= append(&b, ":");
    ok &= append(&b, "buf");
    ok &= append(&b, "fer");
    if (!ok || b.len != 11 || strcmp(b.data, "lc39:buffer") != 0) {
        printf("lc39 result: FAIL len=%zu text='%s'\n", b.len, b.data);
        return 1;
    }
    printf("lc39 result: OK len=%zu text='%s'\n", b.len, b.data);
    return 0;
}
