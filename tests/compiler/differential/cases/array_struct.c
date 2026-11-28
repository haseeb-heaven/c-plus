#include <stdio.h>
struct Item { int key; int val; };
int main(void) {
    struct Item a[4];
    int keys[4] = {515, 35, 83, 118};
    int j;
    for (int i = 0; i < 4; i++) { a[i].key = keys[i]; a[i].val = i + 1; }
    for (int i = 1; i < 4; i++) {
        struct Item t = a[i];
        j = i - 1;
        while (j >= 0 && a[j].key > t.key) { a[j + 1] = a[j]; j--; }
        a[j + 1] = t;
    }
    printf("keys=%d,%d,%d,%d\n", a[0].key, a[1].key, a[2].key, a[3].key);
    printf("vals=%d,%d,%d,%d\n", a[0].val, a[1].val, a[2].val, a[3].val);
    return 0;
}
