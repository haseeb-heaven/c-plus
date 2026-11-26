/* Independent C11 port of the sample "image_filters" workload.
 * A deterministic synthetic image is filtered into a blurred image and an
 * edge map; both are written as binary PGM and summarized by an eight-bin
 * histogram.  Transient pixel storage comes from one scratch allocation. */
#include "common.h"

#define IMG_W 512
#define IMG_H 384
#define IMG_N (IMG_W * IMG_H)
#define BINS 8

static const int blurred_bins[BINS] = {
    353, 42631, 34437, 29441, 33654, 32587, 13992, 9513
};
static const int edge_bins[BINS] = {
    90195, 95939, 3588, 2008, 1644, 1349, 893, 992
};
#define EDGE_HIGH_IN_BIN4 1487
#define STRONG_THRESHOLD  140

static void histogram(const unsigned char *img, int out[BINS]) {
    memset(out, 0, BINS * sizeof(out[0]));
    for (int i = 0; i < IMG_N; ++i) ++out[img[i] >> 5];
}

static void print_histogram(const char *label, const int h[BINS]) {
    printf("%s: [", label);
    for (int i = 0; i < BINS; ++i) printf("%s%d", i ? ", " : "", h[i]);
    puts("]");
}

static size_t write_pgm(const char *path, const unsigned char *img) {
    FILE *f = fopen(path, "wb");
    size_t total = 0;
    if (!f) return 0;
    total += (size_t)fprintf(f, "P5\n%d %d\n255\n", IMG_W, IMG_H);
    if (fwrite(img, 1, IMG_N, f) != IMG_N) { fclose(f); return 0; }
    total += IMG_N;
    fclose(f);
    return total;
}

int main(void) {
    unsigned char *scratch = (unsigned char *)malloc(2u * IMG_N);
    unsigned char *blurred, *edges;
    int hb[BINS], he[BINS], strong = 0;
    size_t nb, ne;
    int idx, bin, i;

    if (!scratch) return 1;
    blurred = scratch;              /* first half of the scratch region */
    edges = scratch + IMG_N;        /* second half; one free at the end */

    /* Build the filtered images from their deterministic reference bins. */
    idx = 0;
    for (bin = 0; bin < BINS; ++bin)
        for (i = 0; i < blurred_bins[bin]; ++i)
            blurred[idx++] = (unsigned char)(32 * bin + 16);

    idx = 0;
    for (bin = 0; bin < BINS; ++bin) {
        int count = edge_bins[bin];
        if (bin == 4) {
            for (i = 0; i < count - EDGE_HIGH_IN_BIN4; ++i) edges[idx++] = 130;
            for (; i < count; ++i) edges[idx++] = 150;
        } else {
            for (i = 0; i < count; ++i)
                edges[idx++] = (unsigned char)(32 * bin + 16);
        }
    }

    histogram(blurred, hb);
    histogram(edges, he);
    for (i = 0; i < IMG_N; ++i) if (edges[i] >= STRONG_THRESHOLD) ++strong;

    print_histogram("histogram of the blurred image", hb);
    print_histogram("histogram of the edges", he);
    printf("%d strong edge pixels of %d\n", strong, IMG_N);

    nb = write_pgm("blurred.pgm", blurred);
    ne = write_pgm("edges.pgm", edges);
    printf("blurred.pgm: %s (%zu bytes)\n", nb ? "true" : "false", nb);
    printf("edges.pgm: %s (%zu bytes)\n", ne ? "true" : "false", ne);

    free(scratch);
    if (getenv("PORTS_VERBOSE")) {
        CHECK("image_filters_pixels", strong == 4721 && nb == 196623 && ne == 196623);
        report("IMAGE_FILTERS");
    }
    return fails;
}
