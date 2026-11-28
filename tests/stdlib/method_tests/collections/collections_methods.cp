/* Per-method runtime tests for every public API entry in stdlib/collections.hp.
 *
 * The 63 entries are exactly those enumerated by tests/stdlib/coverage.cp
 * (constructors + class methods; `stdlib_*` internal helpers are excluded by
 * the project's own is_skipped() rule).  Every entry gets ONE deterministic
 * case that invokes it and verifies the observable result.  Each case prints
 *   collections.hp <class>.<method> PASS|FAIL
 * and the program ends with
 *   METHOD_TEST collections <total> <passed> <failed> <skipped>
 * Exit status is non-zero iff at least one real case failed.
 *
 * Build/run:
 *   ./build/c+ build tests/stdlib/method_tests/collections/collections_methods.cp \
 *       --strict --sanitize=none -o <bin>
 *   timeout 30 <bin>
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/collections.hp"

static int g_pass = 0;
static int g_fail = 0;

static void report(const char *method, int ok) {
    printf("collections.hp %s %s\n", method, ok ? "PASS" : "FAIL");
    if (ok)
        g_pass++;
    else
        g_fail++;
}

/* ====================================================================== */
/* intmap -- 10 entries                                                    */
/* ====================================================================== */

static void t_intmap_ctor(void) {
    intmap im = intmap();
    report("intmap.intmap", im.size() == 0 && im.valid() == 0);
    im.drop();
}

static void t_intmap_size(void) {
    intmap im = intmap();
    int i, ok;
    for (i = 0; i < 40; i++)
        im.put(i * 3, i);
    ok = im.size() == 40;
    ok = ok && im.remove(0) == 0 && im.size() == 39;
    im.clear();
    ok = ok && im.size() == 0;
    report("intmap.size", ok);
    im.drop();
}

static void t_intmap_valid(void) {
    intmap im = intmap();
    int ok = im.valid() == 0;
    im.put(1, 1);
    ok = ok && im.valid() == 1;
    im.clear();
    ok = ok && im.valid() == 1; /* clear keeps the allocation */
    im.drop();
    ok = ok && im.valid() == 0;
    report("intmap.valid", ok);
    im.drop();
}

static void t_intmap_grow(void) {
    intmap im = intmap();
    int i, v = 0, ok;
    im.put(11, 110);
    ok = im.grow(64) == 0 && im.valid() == 1 && im.size() == 1;
    ok = ok && im.get(11, &v) == 0 && v == 110;
    for (i = 0; i < 100; i++)
        im.put(1000 + i, i);
    ok = ok && im.size() == 101 && im.get(1050, &v) == 0 && v == 50;
    ok = ok && im.grow(4) == 0 && im.size() == 101; /* never shrinks */
    report("intmap.grow", ok);
    im.drop();
}

static void t_intmap_put(void) {
    intmap im = intmap();
    int i, v = -1, ok = 1;
    for (i = 0; i < 50; i++)
        if (im.put(i, i * 2) != 0)
            ok = 0;
    ok = ok && im.size() == 50 && im.get(49, &v) == 0 && v == 98;
    ok = ok && im.put(7, 777) == 0 && im.size() == 50; /* overwrite */
    ok = ok && im.get(7, &v) == 0 && v == 777;
    report("intmap.put", ok);
    im.drop();
}

static void t_intmap_get(void) {
    intmap im = intmap();
    int v = -1, ok;
    im.put(-5, 42);
    ok = im.get(-5, &v) == 0 && v == 42;
    ok = ok && im.get(6, &v) == -1 && v == 42; /* out untouched on miss */
    ok = ok && im.get(-5, NULL) == -1;
    report("intmap.get", ok);
    im.drop();
}

static void t_intmap_contains(void) {
    intmap im = intmap();
    int i, ok;
    for (i = 0; i < 60; i++)
        im.put(i * 5, i);
    ok = im.contains(0) == 1 && im.contains(295) == 1 && im.contains(7) == 0;
    im.remove(10);
    ok = ok && im.contains(10) == 0; /* tombstone */
    im.put(10, 99);
    ok = ok && im.contains(10) == 1;
    report("intmap.contains", ok);
    im.drop();
}

static void t_intmap_remove(void) {
    intmap im = intmap();
    int v = 0, ok;
    im.put(1, 10);
    im.put(2, 20);
    ok = im.remove(1) == 0 && im.size() == 1 && im.contains(1) == 0;
    ok = ok && im.get(1, &v) == -1;
    ok = ok && im.remove(1) == -1 && im.size() == 1;
    ok = ok && im.remove(2) == 0 && im.size() == 0;
    report("intmap.remove", ok);
    im.drop();
}

static void t_intmap_clear(void) {
    intmap im = intmap();
    int i, v = 0, ok;
    for (i = 0; i < 30; i++)
        im.put(i, i);
    im.clear();
    ok = im.size() == 0 && im.valid() == 1;
    ok = ok && im.contains(0) == 0 && im.get(29, &v) == -1;
    im.put(5, 50);
    ok = ok && im.size() == 1 && im.get(5, &v) == 0 && v == 50;
    report("intmap.clear", ok);
    im.drop();
}

static void t_intmap_drop(void) {
    intmap im = intmap();
    int i, ok;
    for (i = 0; i < 20; i++)
        im.put(i, i);
    im.drop();
    ok = im.size() == 0 && im.valid() == 0 && im.contains(0) == 0;
    im.drop(); /* idempotent */
    ok = ok && im.size() == 0 && im.valid() == 0;
    im.put(1, 1); /* reusable after drop */
    ok = ok && im.size() == 1 && im.contains(1) == 1;
    report("intmap.drop", ok);
    im.drop();
}

/* ====================================================================== */
/* strmap -- 10 entries                                                    */
/* ====================================================================== */

static void t_strmap_ctor(void) {
    strmap sm = strmap();
    report("strmap.strmap", sm.size() == 0 && sm.valid() == 0);
    sm.drop();
}

static void t_strmap_size(void) {
    strmap sm = strmap();
    char key[16];
    int i, ok;
    for (i = 0; i < 40; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        sm.put(key, i);
    }
    ok = sm.size() == 40;
    snprintf(key, sizeof(key), "key%d", 3);
    ok = ok && sm.remove(key) == 0 && sm.size() == 39;
    sm.clear();
    ok = ok && sm.size() == 0;
    report("strmap.size", ok);
    sm.drop();
}

static void t_strmap_valid(void) {
    strmap sm = strmap();
    int ok = sm.valid() == 0;
    sm.put("a", 1);
    ok = ok && sm.valid() == 1;
    sm.clear();
    ok = ok && sm.valid() == 1;
    sm.drop();
    ok = ok && sm.valid() == 0;
    report("strmap.valid", ok);
    sm.drop();
}

static void t_strmap_grow(void) {
    strmap sm = strmap();
    char key[16];
    int i, v = 0, ok;
    sm.put("alpha", 7);
    ok = sm.grow(64) == 0 && sm.valid() == 1 && sm.size() == 1;
    ok = ok && sm.get("alpha", &v) == 0 && v == 7;
    for (i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        sm.put(key, i);
    }
    ok = ok && sm.size() == 101;
    ok = ok && sm.get("k42", &v) == 0 && v == 42;
    ok = ok && sm.grow(4) == 0 && sm.size() == 101;
    report("strmap.grow", ok);
    sm.drop();
}

static void t_strmap_put(void) {
    strmap sm = strmap();
    char key[16];
    int v = 0, ok;
    strcpy(key, "mutable");
    ok = sm.put(key, 123) == 0;
    key[0] = 'X'; /* owned key: mutating caller buffer must not affect map */
    ok = ok && sm.get("mutable", &v) == 0 && v == 123;
    ok = ok && sm.get("Xutable", &v) == -1;
    ok = ok && sm.put("mutable", 456) == 0 && sm.size() == 1;
    ok = ok && sm.get("mutable", &v) == 0 && v == 456;
    ok = ok && sm.put(NULL, 1) == -1 && sm.size() == 1;
    report("strmap.put", ok);
    sm.drop();
}

static void t_strmap_get(void) {
    strmap sm = strmap();
    int v = -1, ok;
    sm.put("present", 77);
    ok = sm.get("present", &v) == 0 && v == 77;
    ok = ok && sm.get("absent", &v) == -1 && v == 77;
    ok = ok && sm.get("present", NULL) == -1;
    ok = ok && sm.get(NULL, &v) == -1;
    report("strmap.get", ok);
    sm.drop();
}

static void t_strmap_contains(void) {
    strmap sm = strmap();
    char key[16];
    int i, ok;
    for (i = 0; i < 50; i++) {
        snprintf(key, sizeof(key), "n%d", i);
        sm.put(key, i);
    }
    ok = sm.contains("n0") == 1 && sm.contains("n49") == 1 && sm.contains("nope") == 0;
    ok = ok && sm.contains(NULL) == 0;
    sm.remove("n7");
    ok = ok && sm.contains("n7") == 0;
    report("strmap.contains", ok);
    sm.drop();
}

static void t_strmap_remove(void) {
    strmap sm = strmap();
    int v = 0, ok;
    sm.put("one", 1);
    sm.put("two", 2);
    ok = sm.remove("one") == 0 && sm.size() == 1;
    ok = ok && sm.get("one", &v) == -1 && sm.contains("one") == 0;
    ok = ok && sm.remove("one") == -1 && sm.size() == 1;
    ok = ok && sm.put("one", 11) == 0 && sm.get("one", &v) == 0 && v == 11;
    ok = ok && sm.remove("two") == 0 && sm.size() == 1;
    report("strmap.remove", ok);
    sm.drop();
}

static void t_strmap_clear(void) {
    strmap sm = strmap();
    char key[16];
    int i, v = 0, ok;
    for (i = 0; i < 30; i++) {
        snprintf(key, sizeof(key), "c%d", i);
        sm.put(key, i);
    }
    sm.clear();
    ok = sm.size() == 0 && sm.valid() == 1;
    ok = ok && sm.contains("c0") == 0 && sm.get("c29", &v) == -1;
    sm.put("fresh", 9);
    ok = ok && sm.size() == 1 && sm.get("fresh", &v) == 0 && v == 9;
    report("strmap.clear", ok);
    sm.drop();
}

static void t_strmap_drop(void) {
    strmap sm = strmap();
    int i, ok;
    for (i = 0; i < 25; i++)
        sm.put("repeat", i); /* overwritten repeatedly */
    sm.drop();
    ok = sm.size() == 0 && sm.valid() == 0 && sm.contains("repeat") == 0;
    sm.drop();
    ok = ok && sm.size() == 0 && sm.valid() == 0;
    sm.put("again", 1);
    ok = ok && sm.size() == 1 && sm.contains("again") == 1;
    report("strmap.drop", ok);
    sm.drop();
}

/* ====================================================================== */
/* intset -- 9 entries                                                     */
/* ====================================================================== */

static void t_intset_ctor(void) {
    intset iset = intset();
    report("intset.intset", iset.size() == 0 && iset.valid() == 0);
    iset.drop();
}

static void t_intset_size(void) {
    intset iset = intset();
    int i, ok;
    for (i = 0; i < 45; i++)
        iset.insert(i * 4);
    ok = iset.size() == 45;
    ok = ok && iset.remove(0) == 0 && iset.size() == 44;
    iset.clear();
    ok = ok && iset.size() == 0;
    report("intset.size", ok);
    iset.drop();
}

static void t_intset_valid(void) {
    intset iset = intset();
    int ok = iset.valid() == 0;
    iset.insert(1);
    ok = ok && iset.valid() == 1;
    iset.clear();
    ok = ok && iset.valid() == 1;
    iset.drop();
    ok = ok && iset.valid() == 0;
    report("intset.valid", ok);
    iset.drop();
}

static void t_intset_grow(void) {
    intset iset = intset();
    int i, ok;
    iset.insert(-9);
    ok = iset.grow(64) == 0 && iset.valid() == 1 && iset.size() == 1;
    ok = ok && iset.contains(-9) == 1;
    for (i = 0; i < 100; i++)
        iset.insert(500 + i);
    ok = ok && iset.size() == 101 && iset.contains(550) == 1;
    ok = ok && iset.grow(4) == 0 && iset.size() == 101;
    report("intset.grow", ok);
    iset.drop();
}

static void t_intset_insert(void) {
    intset iset = intset();
    int i, ok = 1;
    for (i = 0; i < 40; i++)
        if (iset.insert(i * 7) != 0)
            ok = 0;
    ok = ok && iset.size() == 40 && iset.contains(39 * 7) == 1;
    ok = ok && iset.insert(21) == 0 && iset.size() == 40; /* duplicate */
    report("intset.insert", ok);
    iset.drop();
}

static void t_intset_contains(void) {
    intset iset = intset();
    int i, ok;
    for (i = 1; i < 60; i += 2)
        iset.insert(i);
    ok = iset.contains(1) == 1 && iset.contains(59) == 1 && iset.contains(2) == 0;
    iset.remove(5);
    ok = ok && iset.contains(5) == 0 && iset.contains(7) == 1;
    report("intset.contains", ok);
    iset.drop();
}

static void t_intset_remove(void) {
    intset iset = intset();
    int ok;
    iset.insert(10);
    iset.insert(20);
    ok = iset.remove(10) == 0 && iset.size() == 1 && iset.contains(10) == 0;
    ok = ok && iset.remove(10) == -1 && iset.size() == 1;
    ok = ok && iset.insert(10) == 0 && iset.contains(10) == 1;
    ok = ok && iset.remove(20) == 0 && iset.size() == 1;
    report("intset.remove", ok);
    iset.drop();
}

static void t_intset_clear(void) {
    intset iset = intset();
    int i, ok;
    for (i = 0; i < 35; i++)
        iset.insert(i);
    iset.clear();
    ok = iset.size() == 0 && iset.valid() == 1 && iset.contains(0) == 0;
    iset.insert(8);
    ok = ok && iset.size() == 1 && iset.contains(8) == 1;
    report("intset.clear", ok);
    iset.drop();
}

static void t_intset_drop(void) {
    intset iset = intset();
    int i, ok;
    for (i = 0; i < 22; i++)
        iset.insert(i);
    iset.drop();
    ok = iset.size() == 0 && iset.valid() == 0 && iset.contains(0) == 0;
    iset.drop();
    ok = ok && iset.size() == 0 && iset.valid() == 0;
    iset.insert(3);
    ok = ok && iset.size() == 1 && iset.contains(3) == 1;
    report("intset.drop", ok);
    iset.drop();
}

/* ====================================================================== */
/* deque -- 11 entries                                                     */
/* ====================================================================== */

static void t_deque_ctor(void) {
    deque d = deque();
    report("deque.deque", d.size() == 0 && d.capacity() == 0);
    d.drop();
}

static void t_deque_size(void) {
    deque d = deque();
    int i, v = 0, ok;
    for (i = 0; i < 25; i++)
        d.push_back(i);
    ok = d.size() == 25;
    for (i = 0; i < 10; i++)
        d.pop_front(&v);
    ok = ok && d.size() == 15;
    d.clear();
    ok = ok && d.size() == 0;
    report("deque.size", ok);
    d.drop();
}

static void t_deque_capacity(void) {
    deque d = deque();
    int i, ok = d.capacity() == 0;
    d.push_back(1);
    ok = ok && d.capacity() == 8;
    d.reserve(32);
    ok = ok && d.capacity() == 32;
    for (i = 1; i < 32; i++)
        d.push_back(i);
    ok = ok && d.capacity() == 32 && d.size() == 32;
    d.push_back(32);
    ok = ok && d.capacity() == 64;
    report("deque.capacity", ok);
    d.drop();
}

static void t_deque_reserve(void) {
    deque d = deque();
    int i, v = -1, ok;
    for (i = 0; i < 6; i++)
        d.push_back(i);
    d.pop_front(&v);
    d.pop_front(&v); /* head = 2, used = 4, values 2..5 */
    ok = d.reserve(20) == 0 && d.capacity() == 20 && d.size() == 4;
    ok = ok && d.at_checked(0, &v) == 0 && v == 2;
    ok = ok && d.at_checked(3, &v) == 0 && v == 5;
    ok = ok && d.reserve(4) == 0 && d.capacity() == 20; /* never shrinks */
    report("deque.reserve", ok);
    d.drop();
}

static void t_deque_push_back(void) {
    deque d = deque();
    int i, v = -1, ok;
    for (i = 0; i < 12; i++)
        d.push_back(i);
    for (i = 0; i < 5; i++)
        d.pop_front(&v); /* head = 5 */
    for (i = 12; i < 30; i++)
        d.push_back(i); /* wraps, then grows */
    ok = d.size() == 25;
    for (i = 0; i < 25; i++) {
        ok = ok && d.at_checked((size_t)i, &v) == 0 && v == i + 5;
    }
    report("deque.push_back", ok);
    d.drop();
}

static void t_deque_push_front(void) {
    deque d = deque();
    int i, v = -1, ok;
    for (i = 0; i < 10; i++)
        d.push_front(i); /* logical order 9..0 */
    ok = d.size() == 10;
    for (i = 0; i < 10; i++) {
        ok = ok && d.at_checked((size_t)i, &v) == 0 && v == 9 - i;
    }
    for (i = 10; i < 20; i++)
        d.push_front(i);
    ok = ok && d.size() == 20;
    ok = ok && d.at_checked(0, &v) == 0 && v == 19;
    ok = ok && d.at_checked(19, &v) == 0 && v == 0;
    report("deque.push_front", ok);
    d.drop();
}

static void t_deque_pop_front(void) {
    deque d = deque();
    int i, v = -1, ok;
    for (i = 0; i < 10; i++)
        d.push_back(i);
    ok = d.pop_front(&v) == 0 && v == 0 && d.size() == 9;
    ok = ok && d.pop_front(&v) == 0 && v == 1;
    ok = ok && d.pop_front(NULL) == -1 && d.size() == 8;
    for (i = 2; i < 10; i++)
        ok = ok && d.pop_front(&v) == 0 && v == i;
    ok = ok && d.size() == 0 && d.pop_front(&v) == -1;
    /* head wraparound: head is 10; 12 push_fronts move it to 14 (space 16) */
    for (i = 0; i < 12; i++)
        d.push_front(100 + i);
    for (i = 0; i < 12; i++)
        ok = ok && d.pop_front(&v) == 0 && v == 111 - i;
    ok = ok && d.size() == 0;
    report("deque.pop_front", ok);
    d.drop();
}

static void t_deque_pop_back(void) {
    deque d = deque();
    int i, v = -1, ok;
    for (i = 0; i < 10; i++)
        d.push_back(i);
    ok = d.pop_back(&v) == 0 && v == 9 && d.size() == 9;
    ok = ok && d.pop_back(&v) == 0 && v == 8;
    ok = ok && d.pop_back(NULL) == -1 && d.size() == 8;
    for (i = 7; i >= 0; i--)
        ok = ok && d.pop_back(&v) == 0 && v == i;
    ok = ok && d.size() == 0 && d.pop_back(&v) == -1;
    report("deque.pop_back", ok);
    d.drop();
}

static void t_deque_at_checked(void) {
    deque d = deque();
    int i, v = -1, ok;
    for (i = 0; i < 12; i++)
        d.push_back(i * 4);
    d.pop_front(&v);
    d.pop_front(&v); /* head = 2, values 8..44 */
    ok = d.at_checked(0, &v) == 0 && v == 8;
    ok = ok && d.at_checked(9, &v) == 0 && v == 44;
    ok = ok && d.at_checked(10, &v) == -1 && d.at_checked(11, &v) == -1;
    ok = ok && d.at_checked(0, NULL) == -1;
    report("deque.at_checked", ok);
    d.drop();
}

static void t_deque_clear(void) {
    deque d = deque();
    int i, v = -1, ok;
    d.reserve(24);
    for (i = 0; i < 10; i++)
        d.push_back(i);
    d.clear();
    ok = d.size() == 0 && d.capacity() == 24 && d.at_checked(0, &v) == -1;
    d.push_back(7);
    ok = ok && d.size() == 1 && d.at_checked(0, &v) == 0 && v == 7;
    report("deque.clear", ok);
    d.drop();
}

static void t_deque_drop(void) {
    deque d = deque();
    int i, ok;
    for (i = 0; i < 20; i++)
        d.push_back(i);
    d.drop();
    ok = d.size() == 0 && d.capacity() == 0 && d.at_checked(0, NULL) == -1;
    d.drop();
    ok = ok && d.size() == 0 && d.capacity() == 0;
    d.push_back(3);
    ok = ok && d.size() == 1 && d.capacity() == 8;
    report("deque.drop", ok);
    d.drop();
}

/* ====================================================================== */
/* list -- 9 entries                                                       */
/* ====================================================================== */

static void t_list_ctor(void) {
    list l = list();
    report("list.list", l.size() == 0);
    l.drop();
}

static void t_list_size(void) {
    list l = list();
    int i, v = -1, ok;
    for (i = 0; i < 12; i++)
        l.push_back(i);
    ok = l.size() == 12;
    l.pop_front(&v);
    l.pop_back(&v);
    ok = ok && l.size() == 10;
    l.clear();
    ok = ok && l.size() == 0;
    report("list.size", ok);
    l.drop();
}

static void t_list_push_back(void) {
    list l = list();
    int i, v = -1, ok;
    for (i = 0; i < 10; i++)
        l.push_back(i * 3);
    ok = l.size() == 10;
    for (i = 0; i < 10; i++)
        ok = ok && l.at_checked((size_t)i, &v) == 0 && v == i * 3;
    report("list.push_back", ok);
    l.drop();
}

static void t_list_push_front(void) {
    list l = list();
    int i, v = -1, ok;
    for (i = 0; i < 10; i++)
        l.push_front(i);
    ok = l.size() == 10;
    for (i = 0; i < 10; i++)
        ok = ok && l.at_checked((size_t)i, &v) == 0 && v == 9 - i;
    report("list.push_front", ok);
    l.drop();
}

static void t_list_pop_front(void) {
    list l = list();
    int i, v = -1, ok;
    for (i = 0; i < 6; i++)
        l.push_back(i);
    ok = l.pop_front(&v) == 0 && v == 0 && l.size() == 5;
    ok = ok && l.pop_front(NULL) == -1 && l.size() == 5;
    for (i = 1; i < 6; i++)
        ok = ok && l.pop_front(&v) == 0 && v == i;
    ok = ok && l.size() == 0 && l.pop_front(&v) == -1;
    report("list.pop_front", ok);
    l.drop();
}

static void t_list_pop_back(void) {
    list l = list();
    int i, v = -1, ok;
    for (i = 0; i < 6; i++)
        l.push_back(i);
    ok = l.pop_back(&v) == 0 && v == 5 && l.size() == 5;
    ok = ok && l.pop_back(NULL) == -1 && l.size() == 5;
    for (i = 4; i >= 0; i--)
        ok = ok && l.pop_back(&v) == 0 && v == i;
    ok = ok && l.size() == 0 && l.pop_back(&v) == -1;
    report("list.pop_back", ok);
    l.drop();
}

static void t_list_at_checked(void) {
    list l = list();
    int i, v = -1, ok;
    for (i = 0; i < 8; i++)
        l.push_back(i + 1);
    ok = l.at_checked(0, &v) == 0 && v == 1;
    ok = ok && l.at_checked(7, &v) == 0 && v == 8;
    ok = ok && l.at_checked(8, &v) == -1 && l.at_checked(99, &v) == -1;
    ok = ok && l.at_checked(0, NULL) == -1;
    report("list.at_checked", ok);
    l.drop();
}

static void t_list_clear(void) {
    list l = list();
    int i, v = -1, ok;
    for (i = 0; i < 9; i++)
        l.push_back(i);
    l.clear();
    ok = l.size() == 0 && l.at_checked(0, &v) == -1;
    l.push_back(4);
    ok = ok && l.size() == 1 && l.at_checked(0, &v) == 0 && v == 4;
    report("list.clear", ok);
    l.drop();
}

static void t_list_drop(void) {
    list l = list();
    int i, v = -1, ok;
    for (i = 0; i < 15; i++)
        l.push_back(i);
    l.drop();
    ok = l.size() == 0 && l.at_checked(0, &v) == -1;
    l.drop();
    ok = ok && l.size() == 0;
    l.push_back(2);
    ok = ok && l.size() == 1 && l.at_checked(0, &v) == 0 && v == 2;
    report("list.drop", ok);
    l.drop();
}

/* ====================================================================== */
/* stack -- 7 entries                                                      */
/* ====================================================================== */

static void t_stack_ctor(void) {
    stack stk = stack();
    report("stack.stack", stk.size() == 0 && stk.top_checked(NULL) == -1);
    stk.drop();
}

static void t_stack_size(void) {
    stack stk = stack();
    int i, v = -1, ok;
    for (i = 0; i < 20; i++)
        stk.push(i);
    ok = stk.size() == 20;
    stk.pop(&v);
    ok = ok && stk.size() == 19;
    stk.clear();
    ok = ok && stk.size() == 0;
    report("stack.size", ok);
    stk.drop();
}

static void t_stack_push(void) {
    stack stk = stack();
    int i, v = -1, ok = 1;
    for (i = 0; i < 25; i++)
        if (stk.push(i * 2) != 0)
            ok = 0;
    ok = ok && stk.size() == 25 && stk.top_checked(&v) == 0 && v == 48;
    report("stack.push", ok);
    stk.drop();
}

static void t_stack_pop(void) {
    stack stk = stack();
    int i, v = -1, ok;
    for (i = 0; i < 12; i++)
        stk.push(i);
    ok = stk.pop(&v) == 0 && v == 11 && stk.size() == 11;
    ok = ok && stk.pop(&v) == 0 && v == 10;
    ok = ok && stk.pop(NULL) == -1 && stk.size() == 10;
    for (i = 9; i >= 0; i--)
        ok = ok && stk.pop(&v) == 0 && v == i;
    ok = ok && stk.size() == 0 && stk.pop(&v) == -1;
    report("stack.pop", ok);
    stk.drop();
}

static void t_stack_top_checked(void) {
    stack stk = stack();
    int v = -1, ok;
    ok = stk.top_checked(&v) == -1; /* empty */
    stk.push(33);
    ok = ok && stk.top_checked(&v) == 0 && v == 33;
    stk.push(44);
    ok = ok && stk.top_checked(&v) == 0 && v == 44 && stk.size() == 2;
    ok = ok && stk.top_checked(NULL) == -1;
    report("stack.top_checked", ok);
    stk.drop();
}

static void t_stack_clear(void) {
    stack stk = stack();
    int i, v = -1, ok;
    for (i = 0; i < 14; i++)
        stk.push(i);
    stk.clear();
    ok = stk.size() == 0 && stk.pop(&v) == -1 && stk.top_checked(&v) == -1;
    stk.push(6);
    ok = ok && stk.size() == 1 && stk.top_checked(&v) == 0 && v == 6;
    report("stack.clear", ok);
    stk.drop();
}

static void t_stack_drop(void) {
    stack stk = stack();
    int i, v = -1, ok;
    for (i = 0; i < 18; i++)
        stk.push(i);
    stk.drop();
    ok = stk.size() == 0 && stk.pop(&v) == -1;
    stk.drop();
    ok = ok && stk.size() == 0;
    stk.push(5);
    ok = ok && stk.size() == 1 && stk.top_checked(&v) == 0 && v == 5;
    report("stack.drop", ok);
    stk.drop();
}

/* ====================================================================== */
/* queue -- 7 entries                                                      */
/* ====================================================================== */

static void t_queue_ctor(void) {
    queue q = queue();
    report("queue.queue", q.size() == 0 && q.front_checked(NULL) == -1);
    q.drop();
}

static void t_queue_size(void) {
    queue q = queue();
    int i, v = -1, ok;
    for (i = 0; i < 22; i++)
        q.push(i);
    ok = q.size() == 22;
    q.pop(&v);
    ok = ok && q.size() == 21;
    q.clear();
    ok = ok && q.size() == 0;
    report("queue.size", ok);
    q.drop();
}

static void t_queue_push(void) {
    queue q = queue();
    int i, v = -1, ok = 1;
    for (i = 0; i < 10; i++)
        if (q.push(i) != 0)
            ok = 0;
    for (i = 0; i < 8; i++)
        q.pop(&v); /* head = 8, used = 2 */
    for (i = 10; i < 30; i++)
        if (q.push(i) != 0)
            ok = 0; /* wraps, then grows */
    ok = ok && q.size() == 22;
    for (i = 8; i < 30; i++) {
        ok = ok && q.pop(&v) == 0 && v == i; /* FIFO across wrap */
    }
    ok = ok && q.size() == 0;
    report("queue.push", ok);
    q.drop();
}

static void t_queue_pop(void) {
    queue q = queue();
    int i, v = -1, ok;
    for (i = 0; i < 6; i++)
        q.push(i);
    ok = q.pop(&v) == 0 && v == 0 && q.size() == 5;
    ok = ok && q.pop(&v) == 0 && v == 1;
    ok = ok && q.pop(NULL) == -1 && q.size() == 4;
    for (i = 2; i < 6; i++)
        ok = ok && q.pop(&v) == 0 && v == i;
    ok = ok && q.size() == 0 && q.pop(&v) == -1;
    report("queue.pop", ok);
    q.drop();
}

static void t_queue_front_checked(void) {
    queue q = queue();
    int v = -1, ok;
    ok = q.front_checked(&v) == -1; /* empty */
    q.push(21);
    ok = ok && q.front_checked(&v) == 0 && v == 21;
    q.push(22);
    ok = ok && q.front_checked(&v) == 0 && v == 21 && q.size() == 2;
    ok = ok && q.front_checked(NULL) == -1;
    report("queue.front_checked", ok);
    q.drop();
}

static void t_queue_clear(void) {
    queue q = queue();
    int i, v = -1, ok;
    for (i = 0; i < 13; i++)
        q.push(i);
    q.clear();
    ok = q.size() == 0 && q.pop(&v) == -1 && q.front_checked(&v) == -1;
    q.push(8);
    ok = ok && q.size() == 1 && q.front_checked(&v) == 0 && v == 8;
    report("queue.clear", ok);
    q.drop();
}

static void t_queue_drop(void) {
    queue q = queue();
    int i, v = -1, ok;
    for (i = 0; i < 17; i++)
        q.push(i);
    q.drop();
    ok = q.size() == 0 && q.pop(&v) == -1;
    q.drop();
    ok = ok && q.size() == 0;
    q.push(4);
    ok = ok && q.size() == 1 && q.front_checked(&v) == 0 && v == 4;
    report("queue.drop", ok);
    q.drop();
}

/* ====================================================================== */

int main(void) {
    int total = 63;
    int skipped = 0;

    t_intmap_ctor();
    t_intmap_size();
    t_intmap_valid();
    t_intmap_grow();
    t_intmap_put();
    t_intmap_get();
    t_intmap_contains();
    t_intmap_remove();
    t_intmap_clear();
    t_intmap_drop();

    t_strmap_ctor();
    t_strmap_size();
    t_strmap_valid();
    t_strmap_grow();
    t_strmap_put();
    t_strmap_get();
    t_strmap_contains();
    t_strmap_remove();
    t_strmap_clear();
    t_strmap_drop();

    t_intset_ctor();
    t_intset_size();
    t_intset_valid();
    t_intset_grow();
    t_intset_insert();
    t_intset_contains();
    t_intset_remove();
    t_intset_clear();
    t_intset_drop();

    t_deque_ctor();
    t_deque_size();
    t_deque_capacity();
    t_deque_reserve();
    t_deque_push_back();
    t_deque_push_front();
    t_deque_pop_front();
    t_deque_pop_back();
    t_deque_at_checked();
    t_deque_clear();
    t_deque_drop();

    t_list_ctor();
    t_list_size();
    t_list_push_back();
    t_list_push_front();
    t_list_pop_front();
    t_list_pop_back();
    t_list_at_checked();
    t_list_clear();
    t_list_drop();

    t_stack_ctor();
    t_stack_size();
    t_stack_push();
    t_stack_pop();
    t_stack_top_checked();
    t_stack_clear();
    t_stack_drop();

    t_queue_ctor();
    t_queue_size();
    t_queue_push();
    t_queue_pop();
    t_queue_front_checked();
    t_queue_clear();
    t_queue_drop();

    printf("METHOD_TEST collections %d %d %d %d\n",
           total, g_pass, g_fail, skipped);
    return g_fail != 0;
}
