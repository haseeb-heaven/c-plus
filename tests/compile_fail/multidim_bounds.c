/* Invalid: i reaches 19, so i/4 reaches 4 on a 4x4 array. */
static int g[4][4];
int main(void) {
    for (int i = 0; i < 20; i++)
        g[i/4][i%4] = i;
    return 0;
}
