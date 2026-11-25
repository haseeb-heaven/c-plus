#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/c_standards/fixtures/std_version.c"
/* Backend standard-forwarding probe: output depends on the selected std.
 * Compared only against /usr/bin/clang, the backend c+ uses by default.
 */
#include <stdio.h>

int main(void)
{
#ifdef __STDC__
    printf("stdc=1\n");
#else
    printf("stdc=0\n");
#endif
#ifdef __STDC_VERSION__
    printf("version=%ld\n", (long)__STDC_VERSION__);
#else
    printf("version=none\n");
#endif
    return 0;
}
