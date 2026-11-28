/* C+ RAII & container safety scenarios for the 45-scenario memory-safety matrix. */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"
#include "stdlib/collections.hp"
#include "stdlib/file.hp"
#include "stdlib/fs.hp"
#include "stdlib/thread.hp"

static int early_return_helper(int bail) {
    vector v = vector();
    v.push_back(1);
    if (bail)
        return 7; /* C+ RAII drops v automatically on early return */
    v.push_back(2);
    return 0;
}

int main(void) {
    error err = error();
    int failures = 0;
    int out = 0;

    /* S26-S28: Scope exit, early return, break/continue, nested scopes */
    if (early_return_helper(1) != 7)
        failures++;
    for (int i = 0; i < 4; ++i) {
        vector lv = vector();
        lv.push_back(i);
        if (i == 1)
            continue;
        if (i == 3)
            break;
    }
    {
        string outer = string("outer");
        {
            string inner = string("inner");
            if (!inner.valid())
                failures++;
        }
        if (!outer.equals("outer"))
            failures++;
    }

    /* S29-S33: Slice invalidation, UTF-8 rejection, container OOM rollback */
    {
        vector v = vector();
        v.push_back(10);
        slice s = v.view();
        if (s.stale() || !s.valid())
            failures++;
        for (int i = 0; i < 16; ++i)
            v.push_back(i);
        if (!s.stale() || s.valid() || s.at_checked(0, &out) == 0)
            failures++;

        string bad_utf8 = string("\xF4\x90\x80\x80");
        string good_utf8 = string("ok-\xCF\x80");
        if (bad_utf8.is_valid_utf8() || !good_utf8.is_valid_utf8())
            failures++;

        intmap im = intmap();
        strmap sm = strmap();
        im.put(5, 55);
        sm.put("k", 77);
        stdlib_alloc_fail_after(1);
        if (im.grow(64) == 0 || sm.grow(64) == 0)
            failures++;
        stdlib_alloc_fail_reset();
        if (im.get(5, &out) != 0 || out != 55)
            failures++;
        if (sm.get("k", &out) != 0 || out != 77)
            failures++;
    }

    /* S34-S37: New containers + explicit drop + file/temp/mkdtemp safety */
    {
        intset iset = intset();
        deque dq = deque();
        list lst = list();
        stack st = stack();
        queue q = queue();
        iset.insert(1);
        dq.push_front(2);
        lst.push_back(3);
        st.push(4);
        q.push(5);
        if (iset.size() != 1 || dq.size() != 1 || lst.size() != 1 || st.size() != 1 || q.size() != 1)
            failures++;

        file tf = file_temp(&err);
        char dir_tmpl[] = "/tmp/cplus_scen_dir_XXXXXX";
        if (!tf.is_open() || tf.close(&err) != STDLIB_OK || tf.close(&err) == STDLIB_OK)
            failures++;
        if (fs_mkdtemp(dir_tmpl, sizeof(dir_tmpl), &err) != STDLIB_OK)
            failures++;
        else
            fs_remove(dir_tmpl, &err);
    }

    /* S38-S40: Guard unlock + drop safety (no double unlock), telemetry */
    {
        mutex m = mutex();
        condvar cv = condvar();
        if (!m.is_valid() || !cv.is_valid())
            failures++;
        {
            guard g = guard(&m);
            g.unlock();
            if (g.holds())
                failures++;
        }
        cv.drop();
        m.drop();
    }

    print("test_scenarios_cp", failures == 0 ? 1 : 0);
    return failures == 0 ? 0 : 1;
}
