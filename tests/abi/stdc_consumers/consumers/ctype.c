/* Consumer: <ctype.h> character classification and conversion. */
#include <ctype.h>
int stdc_ctype_probe(void)
{
    return isalpha('a') + isdigit('7') + (toupper('z') == 'Z') + (isspace(' ') != 0);
}
