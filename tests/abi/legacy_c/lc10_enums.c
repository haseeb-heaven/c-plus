/* lc10: enums -- explicit and implicit enumerator values, switch dispatch
 * and conversion to/from int.
 */
#include <stdio.h>

enum color {
    RED = 1,
    GREEN,
    BLUE = 5,
    CYAN
};

static const char *color_name(enum color c)
{
    switch (c) {
    case RED:
        return "RED";
    case GREEN:
        return "GREEN";
    case BLUE:
        return "BLUE";
    case CYAN:
        return "CYAN";
    default:
        return "?";
    }
}

int main(void)
{
    enum color c = GREEN;
    int raw = (int)BLUE;
    enum color back = (enum color)raw;
    int ok = 1;

    ok &= (RED == 1 && GREEN == 2 && BLUE == 5 && CYAN == 6);
    ok &= (back == BLUE);
    ok &= (color_name(c)[0] == 'G');
    ok &= (color_name(BLUE)[0] == 'B');

    printf("lc10_enums: %s values=%d,%d,%d,%d name=%s\n",
           ok ? "OK" : "FAIL", RED, GREEN, BLUE, CYAN, color_name(c));
    return ok ? 0 : 1;
}
