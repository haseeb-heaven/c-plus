/* Issue #67: member calls through nested receivers must lower to valid C.
 * Covers `x.f.m(args)`, `x.f.m()` (zero args), `x.p->m(args)`, deeper
 * `a.b.c.m(args)` chains, and `this->p->m(args)` from a class method. */
#include <stdio.h>

class Counter {
    int val;
    void add(int delta) { this->val += delta; }
    void inc(void) { this->val += 1; }
    int get(void) { return this->val; }
};

struct Inner {
    Counter c;
};

struct Outer {
    struct Inner b;
};

struct Holder {
    Counter *ptr;
};

class Self {
    Counter c;
    Counter *ptr;
    void bump(void) {
        this->c.add(4);
        this->ptr->add(6);
    }
    int total(void) { return this->c.get() + this->ptr->get(); }
};

int main(void) {
    struct Inner w;
    w.c.val = 0;
    w.c.add(5);
    w.c.inc();
    printf("w.c=%d\n", w.c.get());

    struct Outer a;
    a.b.c.val = 0;
    a.b.c.add(7);
    printf("a.b.c=%d\n", a.b.c.get());

    Counter p = {0};
    struct Holder h;
    h.ptr = &p;
    h.ptr->add(5);
    printf("w.ptr=%d\n", h.ptr->get());

    Counter q = {0};
    Self s;
    s.c.val = 1;
    s.ptr = &q;
    s.bump();
    printf("self=%d\n", s.total());
    return 0;
}
