#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/c_standards/fixtures/io_formatting.c"
/* Deterministic printf formatting; valid in every C standard under test. */
#include <stdio.h>

int main(void)
{
    printf("int=%d plus=%+d pad=%05d\n", 42, 42, 42);
    printf("hex=%x HEX=%X oct=%o\n", 255, 255, 8);
    printf("fixed=%.2f sci=%.1e\n", 1.25, 200.0);
    printf("chars=%c%s\n", 'A', "BC");
    return 0;
}
