/* Independent C11 port of the sample "mandelbrot_threads" workload.
 * The sample renders the same rows by two different traversal orders and
 * reports whether they agree.  No threads are needed for a deterministic port.
 * The complex-plane window and the three-colour escape palette are part of the
 * workload definition. */
#include "common.h"

#define WIDTH  78
#define HEIGHT 36
#define MAX_ITER 512

static int escape_count(double cr, double ci) {
    double zr = 0.0, zi = 0.0;
    for (int n = 0; n < MAX_ITER; ++n) {
        double zr2 = zr * zr, zi2 = zi * zi;
        if (zr2 + zi2 > 4.0) return n;
        zi = 2.0 * zr * zi + ci;
        zr = zr2 - zi2 + cr;
    }
    return MAX_ITER;
}

static char shade(int n) {
    static const char palette[] = " -*";
    return n >= MAX_ITER ? '@' : palette[n % 3];
}

/* One row of the picture.  Row 18 is the real axis; columns step 1/26. */
static void render_row(char *out, int row) {
    const double ci = (row - 18) / 15.0;
    for (int col = 0; col < WIDTH; ++col) {
        const double cr = -2.2 + col / 26.0;
        out[col] = shade(escape_count(cr, ci));
    }
    out[WIDTH] = '\0';
}

int main(void) {
    char parallel[HEIGHT][WIDTH + 1];
    char serial[HEIGHT][WIDTH + 1];
    int match = 1;

    /* "Worker" traversal: rows in order. */
    for (int r = 0; r < HEIGHT; ++r) render_row(parallel[r], r);
    /* Reference traversal: rows in reverse order (independent pass). */
    for (int r = HEIGHT - 1; r >= 0; --r) render_row(serial[r], r);

    for (int r = 0; r < HEIGHT; ++r) {
        if (memcmp(parallel[r], serial[r], WIDTH) != 0) match = 0;
        puts(parallel[r]);
    }
    printf("rows match the serial render: %s\n", match ? "true" : "false");

    if (getenv("PORTS_VERBOSE")) { CHECK("mandelbrot_rows", match); report("MANDELBROT"); }
    return fails;
}
