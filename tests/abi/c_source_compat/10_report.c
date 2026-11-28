/* Legacy 10 -- aggregated report (calloc / strcpy / fprintf / free).
 *
 * Allocates an array of rows with calloc, fills the labels with strcpy, and
 * writes a report to stdout through a FILE* with fprintf. Exercises the
 * printf/fprintf mapping as well as the allocator wrappers in one source.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int  n;
    char label[24];
} row_t;

int main(void)
{
    row_t *rows;
    FILE *out = stdout;
    int i;

    rows = (row_t *)calloc(3, sizeof(row_t));
    if (!rows) return 2;

    strcpy(rows[0].label, "alpha"); rows[0].n = 1;
    strcpy(rows[1].label, "beta");  rows[1].n = 2;
    strcpy(rows[2].label, "gamma"); rows[2].n = 3;

    for (i = 0; i < 3; ++i) {
        fprintf(out, "report: %d %s\n", rows[i].n, rows[i].label);
    }

    free(rows);
    return 0;
}
