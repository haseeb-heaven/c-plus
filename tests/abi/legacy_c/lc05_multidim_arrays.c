/* lc05: multidimensional arrays and 2D array parameters with fixed columns. */
#include <stdio.h>

#define COLS 3

static int grid_total(int rows, const int m[][COLS])
{
    int r;
    int c;
    int s = 0;
    for (r = 0; r < rows; ++r) {
        for (c = 0; c < COLS; ++c) {
            s += m[r][c];
        }
    }
    return s;
}

int main(void)
{
    int m[2][COLS] = {{1, 2, 3}, {4, 5, 6}};
    int ok = 1;

    ok &= (grid_total(2, m) == 21);
    ok &= (m[0][0] == 1 && m[1][2] == 6);
    ok &= (sizeof(m) / sizeof(m[0]) == 2);
    ok &= (sizeof(m[0]) / sizeof(m[0][0]) == COLS);

    printf("lc05_multidim_arrays: %s total=%d dims=%lux%lu corner=%d\n",
           ok ? "OK" : "FAIL", grid_total(2, m),
           (unsigned long)(sizeof(m) / sizeof(m[0])),
           (unsigned long)(sizeof(m[0]) / sizeof(m[0][0])),
           m[1][2]);
    return ok ? 0 : 1;
}
