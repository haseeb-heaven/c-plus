/* lc24: nested flexible-array records reached through a pointer-to-pointer
 * table: each allocation carries its own inline label payload.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct node {
    int id;
    char label[];
};

int main(void)
{
    const char *text[2] = {"aa", "bbbb"};
    size_t lengths[2] = {3, 5};
    struct node **nodes = (struct node **)malloc(2 * sizeof(struct node *));
    int ok = (nodes != NULL);
    size_t i;

    if (ok) {
        nodes[0] = NULL;
        nodes[1] = NULL;
        for (i = 0; i < 2; ++i) {
            nodes[i] = (struct node *)malloc(sizeof(struct node) + lengths[i]);
            if (nodes[i] == NULL) {
                ok = 0;
                break;
            }
            nodes[i]->id = (int)i + 1;
            memcpy(nodes[i]->label, text[i], lengths[i]);
        }
    }

    if (ok) {
        ok &= (nodes[0]->id == 1 && nodes[1]->id == 2);
        ok &= (strcmp(nodes[0]->label, "aa") == 0);
        ok &= (strcmp(nodes[1]->label, "bbbb") == 0);
        printf("lc24_nested_flexible: %s ids=%d,%d labels=%s,%s hdr=%lu\n",
               ok ? "OK" : "FAIL", nodes[0]->id, nodes[1]->id,
               nodes[0]->label, nodes[1]->label,
               (unsigned long)sizeof(struct node));
    } else {
        printf("lc24_nested_flexible: FAIL allocation\n");
    }

    if (nodes != NULL) {
        for (i = 0; i < 2; ++i) {
            free(nodes[i]);
        }
        free(nodes);
    }
    return ok ? 0 : 1;
}
