#include <stdio.h>
int main(void) {
    int m[3][3] = {{43,37,52},{44,41,28},{52,92,58}};
    int trace = 0;
    for (int i = 0; i < 3; i++) trace += m[i][i];
    printf("trace=%d m00=%d m12=%d m21=%d\n", trace, m[0][0], m[1][2], m[2][1]);
    return 0;
}
