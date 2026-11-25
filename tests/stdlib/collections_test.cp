/* Collections: intmap and strmap growth, overwrite, removal, tombstones. */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/collections.hp"

int main(void) {
    error err = error();
    int value = 0;

    intmap m = intmap();
    print("empty", m.size(), m.valid() == 0, m.get(1, &value) == -1,
          m.contains(1) == 0);

    int i;
    for (i = 0; i < 200; i++) {
        if (m.put(i * 7, i) != 0)
            return 1;
    }
    print("grown", m.size(), m.get(7 * 99, &value) == 0 && value == 99,
          m.contains(7) == 1);

    m.put(7, 1234);                                  /* overwrite */
    print("overwrite", m.size(), m.get(7, &value) == 0 && value == 1234,
          m.get(8, &value) == -1);

    print("remove", m.remove(7) == 0, m.contains(7) == 0,
          m.get(7, &value) == -1, m.size());

    m.clear();
    print("clear", m.size(), m.contains(7 * 99) == 0, m.get(0, &value) == -1);

    for (i = 0; i < 50; i++)
        m.put(i, i * 10);
    for (i = 0; i < 50; i += 2)
        m.remove(i);
    {
        int found = 0;
        for (i = 1; i < 50; i += 2)
            if (m.get(i, &value) == 0 && value == i * 10)
                found++;
        print("tombstones", m.size(), found == 25);
    }
    print("grow", m.grow(64) == 0, m.size() == 25, m.get(1, &value) == 0);
    m.drop();
    m.drop();                                        /* double drop */
    print("dropped", m.size(), m.valid() == 0);

    strmap names = strmap();
    print("strmap-empty", names.size(), names.valid() == 0,
          names.contains("x") == 0);
    print("strmap-put", names.put("alpha", 1) == 0, names.put("beta", 2) == 0,
          names.put("alpha", 11) == 0);
    print("strmap-get", names.size() == 2, names.get("alpha", &value) == 0 &&
                                              value == 11,
          names.get("missing", &value) == -1);
    for (i = 0; i < 100; i++) {
        char key[16];
        snprintf(key, sizeof(key), "k%d", i);
        names.put(key, i);
    }
    print("strmap-grown", names.size() == 102, names.contains("k99"),
          names.get("k42", &value) == 0 && value == 42);
    print("strmap-remove", names.remove("alpha") == 0, names.contains("alpha") == 0,
          names.size() == 101);
    names.put("alpha", 5);                           /* reinsert after delete */
    print("strmap-reinsert", names.contains("alpha"),
          names.get("alpha", &value) == 0 && value == 5);
    print("strmap-grow", names.grow(256) == 0, names.get("k42", &value) == 0);
    names.clear();
    print("strmap-clear", names.size(), names.contains("k1") == 0);
    names.drop();
    names.drop();                                    /* double drop */
    print("strmap-dropped", names.size(), names.valid() == 0);
    return 0;
}
