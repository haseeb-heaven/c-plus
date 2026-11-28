/* libFuzzer target: vector, intset, deque, list, stack, queue, intmap, strmap. */
#include <stddef.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    vector v = vector();
    intset iset = intset();
    deque dq = deque();
    list lst = list();
    stack st = stack();
    queue q = queue();
    intmap im = intmap();
    int out = 0;

    for (size_t i = 0; i < size; ++i) {
        int val = (int)(signed char)data[i];
        switch (data[i] % 8u) {
        case 0: v.push_back(val); break;
        case 1: v.insert(i % 4u, val); v.erase(0, &out); break;
        case 2: iset.insert(val); if (i % 5u == 0) iset.remove(val); break;
        case 3: dq.push_back(val); dq.push_front(val); break;
        case 4: dq.pop_front(&out); dq.pop_back(&out); break;
        case 5: lst.push_back(val); lst.pop_front(&out); break;
        case 6: st.push(val); st.pop(&out); break;
        default: q.push(val); q.pop(&out); im.put(val, (int)i); break;
        }
        if (v.size() > 128u) v.clear();
        if (dq.size() > 128u) dq.clear();
    }
    v.drop();
    iset.drop();
    dq.drop();
    lst.drop();
    st.drop();
    q.drop();
    im.drop();
    return 0;
}
