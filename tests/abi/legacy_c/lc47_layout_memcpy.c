/*
 * lc47 - struct patterns: layout offsets and byte-wise copy via memcpy.
 *
 * Independent legacy-C ABI probe: uses offsetof plus a byte buffer to move a
 * struct between storage locations without relying on alignment aliasing.
 */
#include <stdio.h>
#include <stddef.h>
#include <string.h>

struct mixed {
    char tag;
    int count;
    double ratio;
    unsigned char tail[3];
};

int main(void)
{
    struct mixed src;
    struct mixed dst;
    unsigned char bytes[sizeof(struct mixed)];
    size_t off_tag = offsetof(struct mixed, tag);
    size_t off_count = offsetof(struct mixed, count);
    size_t off_ratio = offsetof(struct mixed, ratio);
    size_t off_tail = offsetof(struct mixed, tail);

    memset(&src, 0, sizeof(src));
    src.tag = 'S';
    src.count = 47;
    src.ratio = 3.25;
    src.tail[0] = 9;
    src.tail[1] = 8;
    src.tail[2] = 7;

    memcpy(bytes, &src, sizeof(src));
    memcpy(&dst, bytes, sizeof(dst));

    if (off_tag >= off_count || off_count >= off_ratio ||
        off_ratio >= off_tail || off_tail + 3u > sizeof(struct mixed) ||
        dst.tag != 'S' || dst.count != 47 || dst.ratio != 3.25 ||
        dst.tail[0] != 9u || dst.tail[1] != 8u || dst.tail[2] != 7u) {
        printf("lc47 result: FAIL offsets %zu/%zu/%zu/%zu\n",
               off_tag, off_count, off_ratio, off_tail);
        return 1;
    }
    printf("lc47 result: OK size=%lu offsets=%zu/%zu/%zu/%zu tag=%c count=%d\n",
           (unsigned long)sizeof(struct mixed), off_tag, off_count,
           off_ratio, off_tail, dst.tag, dst.count);
    return 0;
}
