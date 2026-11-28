/* Deterministic driver for the stdlib fuzz targets when libFuzzer is not
 * available.  It feeds every corpus file and then N pseudo-random buffers
 * through LLVMFuzzerTestOneInput under AddressSanitizer. */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint64_t state = 0x9e3779b97f4a7c15ULL;

static uint64_t next_rand(void) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

static long feed_file(const char *path) {
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long size;
    if (f == NULL)
        return 0;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    size = ftell(f);
    if (size < 0 || size > 65536) {
        fclose(f);
        return 0;
    }
    rewind(f);
    buf = (unsigned char *)malloc((size_t)(size > 0 ? size : 1));
    if (buf == NULL) {
        fclose(f);
        return 0;
    }
    if (size > 0 && fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return 0;
    }
    LLVMFuzzerTestOneInput(buf, (size_t)size);
    free(buf);
    fclose(f);
    return 1;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "tests/stdlib/fuzz/corpus";
    long runs = argc > 2 ? strtol(argv[2], NULL, 10) : 500;
    long files = 0;
    DIR *d = opendir(dir);
    if (d != NULL) {
        struct dirent *entry;
        char path[1024];
        while ((entry = readdir(d)) != NULL) {
            if (entry->d_name[0] == '.')
                continue;
            snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
            files += feed_file(path);
        }
        closedir(d);
    }
    {
        unsigned char *buf = (unsigned char *)malloc(2048);
        if (buf == NULL)
            return 1;
        for (long i = 0; i < runs; i++) {
            size_t len = (size_t)(next_rand() % 2048);
            for (size_t k = 0; k < len; k++)
                buf[k] = (unsigned char)(next_rand() & 0xff);
            LLVMFuzzerTestOneInput(buf, len);
        }
        free(buf);
    }
    printf("fuzz: %ld corpus files + %ld random inputs clean\n", files, runs);
    return 0;
}
