/* EXPECT: ACCEPT (positive control).
 *
 * Reason: every operation uses a live container and the checked accessors
 * with in-range indices; each owned container is dropped exactly once after
 * its last use.  c+ check --strict and c+ build --strict --sanitize=none
 * must both succeed with no [CPLUS- diagnostic, and the program must exit 0.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/collections.hp"

int main(void) {
    intmap m = intmap();
    m.put(1, 11);
    m.put(2, 22);
    int out = -1;
    int rc = m.get(2, &out);
    print("intmap", "size", m.size(), "get", rc, "out", out,
          "contains1", m.contains(1));
    m.remove(1);
    print("intmap-after-remove", m.size(), m.contains(1));
    m.drop();

    strmap sm = strmap();
    sm.put("alpha", 7);
    sm.put("beta", 8);
    out = -1;
    rc = sm.get("beta", &out);
    print("strmap", "size", sm.size(), "get", rc, "out", out,
          "contains-alpha", sm.contains("alpha"));
    sm.remove("alpha");
    print("strmap-after-remove", sm.size());
    sm.drop();

    intset s = intset();
    s.insert(7);
    s.insert(9);
    print("intset", s.size(), s.contains(7), s.contains(8));
    s.remove(7);
    print("intset-after-remove", s.size(), s.contains(7));
    s.drop();

    deque d = deque();
    d.push_back(10);
    d.push_front(5);
    out = -1;
    rc = d.at_checked(1, &out);
    print("deque", "size", d.size(), "cap", d.capacity(), "at", rc, "out", out);
    int popped = -1;
    d.pop_front(&popped);
    print("deque-pop", popped, "size", d.size());
    d.drop();

    stack st = stack();
    st.push(1);
    st.push(2);
    out = -1;
    rc = st.top_checked(&out);
    print("stack", "size", st.size(), "top", rc, "out", out);
    st.pop(&out);
    print("stack-pop", out, "size", st.size());
    st.drop();

    queue q = queue();
    q.push(3);
    q.push(4);
    out = -1;
    rc = q.front_checked(&out);
    print("queue", "size", q.size(), "front", rc, "out", out);
    q.pop(&out);
    print("queue-pop", out, "size", q.size());
    q.drop();

    list l = list();
    l.push_back(6);
    l.push_front(5);
    out = -1;
    rc = l.at_checked(1, &out);
    print("list", "size", l.size(), "at", rc, "out", out);
    l.pop_front(&out);
    print("list-pop", out, "size", l.size());
    l.drop();

    return 0;
}
