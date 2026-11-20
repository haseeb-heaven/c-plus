#include "cplus/cplus.h"

int main(void) {
    int n;
    double d;
    char name[32];
    int arr[] = {10, 20, 30};

    scan(n, d, name);
    print("n=", n, "d=", d, "name=", name, "len=", arr.len);
    return 0;
}
