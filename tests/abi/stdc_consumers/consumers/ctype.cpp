/* Consumer: <ctype.h> character classification from C++17. */
#include <ctype.h>
int stdc_ctype_probe()
{
    return isalpha('a') + isdigit('7') + (toupper('z') == 'Z') + (isspace(' ') != 0);
}
