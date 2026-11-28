/* Bug 457 family: command-line text used as a printf format (%n injection). */
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc > 1)
        printf(argv[1]);
    return 0;
}
