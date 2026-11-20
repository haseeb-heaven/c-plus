/* Valid: i/4 in 0..3 and i%4 in 0..3 for a 4x4 array. */
static int g[4][4];
int main(void) {
    for (int i = 0; i < 16; i++)
        g[i/4][i%4] = i;
    int r[4];                 /* declaration inside loop must not be checked */
    for (int i = 0; i < 4; i++)
        r[i] = g[0][i];
    return r[0];
}
