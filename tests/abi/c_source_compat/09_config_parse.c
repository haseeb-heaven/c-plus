/* Legacy 09 -- config line parser (strchr / memcpy / strcpy / printf).
 *
 * Splits "key=value" lines using strchr, copies the key with memcpy and the
 * value with strcpy. Uses only ordinary C spelling throughout.
 */
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *lines[] = {"host=localhost", "port=1971", "mode=legacy"};
    char key[16];
    char val[32];
    int i;

    for (i = 0; i < 3; ++i) {
        const char *eq = strchr(lines[i], '=');
        size_t klen;

        if (!eq) return 3;
        klen = (size_t)(eq - lines[i]);
        memcpy(key, lines[i], klen);
        key[klen] = '\0';
        strcpy(val, eq + 1);

        printf("config_parse: [%s] => [%s]\n", key, val);
    }
    return 0;
}
