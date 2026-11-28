/* Demo J: modern print/scan with no format specifiers and no `&`.
 *
 * print() dispatches each value by its C type; scan() takes lvalues and
 * passes their addresses internally.  A char array is read with a bound. */
#include "cplus/cplus.h"

int main(void) {
    int count;
    double ratio;
    char label[32];
    int values[] = {10, 30, 50};

    print("enter count ratio label:");
    scan(count, ratio, label);
    print("count=", count, "ratio=", ratio, "label=", label,
          "values.len=", values.len);
    return 0;
}
