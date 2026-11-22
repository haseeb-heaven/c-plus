#include <assert.h>
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

static int cb_calls = 0;
static int cb_last_cat = 0;

static void on_stdlib_error(int category, cplus_exit_code code, const char *message) {
    (void)code;
    (void)message;
    cb_calls++;
    cb_last_cat = category;
}

static mutex g_sync_m;
static condvar g_sync_cv;
static int g_sync_ready = 0;
static int g_sync_done = 0;
static int g_sync_value = 2;

static void *worker_wait_signal(void *arg) {
    (void)arg;
    g_sync_m.lock();
    g_sync_ready = 1;
    g_sync_cv.signal();
    while (!g_sync_done) {
        g_sync_cv.wait(&g_sync_m);
    }
    g_sync_value += 40;
    g_sync_m.unlock();
    return NULL;
}

static void *worker_auto_join(void *arg) {
    int *flag = (int *)arg;
    *flag = 99;
    return NULL;
}

int main(void) {
    error err = error();
    stdlib_telemetry_stats tm;
    int failures = 0;
    int out = 0;
    long baseline_seq = 0;

    /* 1. Telemetry & error callbacks */
    stdlib_telemetry_reset();
    stdlib_telemetry_set_enabled(1);
    stdlib_set_error_callback(on_stdlib_error);
    if (!stdlib_telemetry_is_enabled())
        failures++;
    if (strcmp(stdlib_category_name(STDLIB_CATEGORY_OWNERSHIP), "ownership") != 0)
        failures++;
    if (strcmp(stdlib_category_name(STDLIB_CATEGORY_UTF8), "utf8") != 0)
        failures++;

    /* 2. Allocation metadata, query_length, generation, ownership transfer */
    {
        void *blk = stdlib_calloc(4, sizeof(int));
        void *moved = NULL;
        unsigned long long gen1 = 0;
        size_t qcnt = 0;
        if (blk == NULL) {
            failures++;
        } else {
            gen1 = stdlib_alloc_generation(blk);
            if (stdlib_alloc_size(blk) != 4 * sizeof(int))
                failures++;
            if (stdlib_alloc_elem_size(blk) != sizeof(int))
                failures++;
            if (stdlib_alloc_length(blk) != 4)
                failures++;
            if (stdlib_alloc_query_length(blk, sizeof(int), &qcnt) != 0 || qcnt != 4)
                failures++;
            if (!stdlib_alloc_check_generation(blk, gen1))
                failures++;
            if (stdlib_transfer_ownership(&moved, &blk) != 0 || blk != NULL || moved == NULL)
                failures++;
            if (stdlib_alloc_check_generation(moved, gen1))
                failures++;
            if (stdlib_transfer_ownership(&moved, &moved) == 0)
                failures++;
            stdlib_release(moved);
            if (stdlib_alloc_size(moved) != 0)
                failures++;
        }
    }
    print("meta-ok", failures == 0 ? 1 : 0);

    /* 3. Vector insert, erase, copy_from, shrink_to_fit & slice staleness */
    {
        vector v = vector();
        vector v2 = vector();
        v.push_back(10);
        v.push_back(30);
        if (v.insert(1, 20) != 0)
            failures++;
        if (v.insert(99, 1) == 0)
            failures++;
        if (v.size() != 3 || v.at(1) != 20)
            failures++;

        slice s = v.view();
        if (!s.valid() || s.stale() || s.size() != 3)
            failures++;
        /* Cause growth/mutation: push_back increments generation */
        for (int i = 0; i < 16; i++)
            v.push_back(100 + i);
        if (!s.stale() || s.valid() || s.at_checked(0, &out) == 0)
            failures++;

        if (v.erase(1, &out) != 0 || out != 20)
            failures++;
        if (v.erase(999, &out) == 0)
            failures++;
        if (v2.copy_from(&v) != 0 || v2.size() != v.size())
            failures++;
        if (v2.shrink_to_fit() != 0 || v2.capacity() != v2.size())
            failures++;

        /* Vector OOM rollback preserves state */
        stdlib_alloc_fail_at(1);
        size_t old_size = v2.size();
        size_t old_cap = v2.capacity();
        if (v2.reserve(old_cap + 64) == 0)
            failures++;
        if (v2.size() != old_size || v2.capacity() != old_cap)
            failures++;
        stdlib_alloc_fail_reset();

        v.drop();
        v2.drop();
    }
    print("vector-slice-ok", failures == 0 ? 1 : 0);

    /* 4. String validity, UTF-8 validation, copy_from, OOM rollback */
    {
        string s1 = string("hello \xE2\x82\xAC");
        string s2 = string("");
        string_view sv = s1.to_view();
        const char bad_overlong[] = "\xC0\xAF";
        const char bad_surrogate[] = "\xED\xA0\x80";
        const char bad_high[] = "\xF4\x90\x80\x80";
        const char bad_trunc[] = "\xE2\x82";
        if (!s1.valid() || !s1.is_valid_utf8() || !sv.is_valid_utf8())
            failures++;
        if (s1.equals(NULL) || s1.compare(NULL) == 0 || s1.append(NULL) == 0)
            failures++;
        if (s2.copy_from(&s1) != 0 || !s2.equals(s1.c_str()))
            failures++;

        stdlib_alloc_fail_at(1);
        if (s2.reserve(s2.capacity() + 128) == 0)
            failures++;
        if (!s2.equals(s1.c_str()))
            failures++;
        stdlib_alloc_fail_reset();

        if (stdlib_utf8_validate(bad_overlong, 2))
            failures++;
        if (stdlib_utf8_validate(bad_surrogate, 3))
            failures++;
        if (stdlib_utf8_validate(bad_high, 4))
            failures++;
        if (stdlib_utf8_validate(bad_trunc, 2))
            failures++;

        string bad = string(bad_surrogate);
        if (bad.is_valid_utf8())
            failures++;

        s1.drop();
        s2.drop();
        bad.drop();
    }
    print("string-utf8-ok", failures == 0 ? 1 : 0);

    /* 5. Containers: intset, deque, list, stack, queue + OOM rollback */
    {
        intset iset = intset();
        deque dq = deque();
        list lst = list();
        stack st = stack();
        queue q = queue();

        if (iset.insert(42) != 0 || !iset.valid() || iset.insert(42) != 0 || iset.insert(7) != 0)
            failures++;
        if (!iset.contains(42) || !iset.contains(7) || iset.size() != 2)
            failures++;
        if (iset.remove(42) != 0 || iset.contains(42) || iset.size() != 1)
            failures++;
        iset.clear();
        if (iset.size() != 0)
            failures++;
        iset.drop();
        iset.drop();

        dq.push_back(2);
        dq.push_front(1);
        dq.push_back(3);
        if (dq.size() != 3 || dq.at_checked(0, &out) != 0 || out != 1)
            failures++;
        if (dq.pop_front(&out) != 0 || out != 1 || dq.pop_back(&out) != 0 || out != 3)
            failures++;
        dq.clear();
        dq.drop();
        dq.drop();

        lst.push_back(20);
        lst.push_front(10);
        lst.push_back(30);
        if (lst.size() != 3 || lst.at_checked(1, &out) != 0 || out != 20)
            failures++;
        if (lst.pop_front(&out) != 0 || out != 10 || lst.pop_back(&out) != 0 || out != 30)
            failures++;
        lst.clear();
        lst.drop();

        st.push(5);
        st.push(6);
        if (st.size() != 2 || st.top_checked(&out) != 0 || out != 6)
            failures++;
        if (st.pop(&out) != 0 || out != 6 || st.pop(&out) != 0 || out != 5)
            failures++;
        if (st.pop(&out) == 0)
            failures++;
        st.clear();
        st.drop();

        q.push(11);
        q.push(12);
        if (q.size() != 2 || q.front_checked(&out) != 0 || out != 11)
            failures++;
        if (q.pop(&out) != 0 || out != 11 || q.pop(&out) != 0 || out != 12)
            failures++;
        if (q.pop(&out) == 0)
            failures++;
        q.clear();
        q.drop();

        /* OOM injection across intmap & strmap grow rollback */
        intmap im = intmap();
        strmap sm = strmap();
        im.put(1, 100);
        sm.put("alpha", 200);
        stdlib_alloc_fail_after(1);
        if (im.grow(64) == 0)
            failures++;
        stdlib_alloc_fail_reset();
        if (im.get(1, &out) != 0 || out != 100)
            failures++;
        stdlib_alloc_fail_after(2);
        if (sm.grow(64) == 0)
            failures++;
        stdlib_alloc_fail_reset();
        if (sm.get("alpha", &out) != 0 || out != 200)
            failures++;
        im.drop();
        sm.drop();
    }
    print("collections-ok", failures == 0 ? 1 : 0);

    /* 6. file::close, file_temp, fs_mkdtemp */
    {
        file tf = file_temp(&err);
        char buf[32];
        char dir_tmpl[] = "/tmp/cplus_mkdtemp_XXXXXX";
        if (!tf.is_open()) {
            failures++;
        } else {
            memset(buf, 0, sizeof(buf));
            tf.write("tmpdata");
            tf.seek(0, SEEK_SET, &err);
            if (tf.read(buf, sizeof(buf), &err) != STDLIB_OK || strncmp(buf, "tmpdata", 7) != 0)
                failures++;
            if (tf.close(&err) != 0 || tf.is_open())
                failures++;
            /* Double close must fail cleanly without UAF */
            if (tf.close(&err) == 0)
                failures++;
            tf.drop();
        }
        if (fs_mkdtemp(dir_tmpl, sizeof(dir_tmpl), &err) != 0 || !fs_is_dir(dir_tmpl))
            failures++;
        else
            fs_remove(dir_tmpl, &err);
    }
    print("file-fs-ok", failures == 0 ? 1 : 0);

    /* 7. Threading: mutex, guard unlock+drop, condvar, thread */
    {
        thread t1 = thread();
        g_sync_m = mutex();
        g_sync_cv = condvar();
        g_sync_ready = 0;
        g_sync_done = 0;
        g_sync_value = 2;

        if (!g_sync_m.is_valid() || !g_sync_cv.is_valid())
            failures++;
        if (t1.is_joinable())
            failures++;
        if (t1.start(worker_wait_signal, NULL, &err) != 0 || !t1.is_joinable())
            failures++;

        {
            guard g = guard(&g_sync_m);
            if (!g.holds())
                failures++;
            while (!g_sync_ready) {
                g_sync_cv.wait(&g_sync_m);
            }
            g_sync_done = 1;
            g_sync_cv.broadcast();
            g.unlock();
            if (g.holds())
                failures++;
        }
        if (t1.join(NULL, &err) != 0 || t1.is_joinable() || g_sync_value != 42)
            failures++;

        /* thread auto-join in destructor */
        static int auto_flag = 0;
        {
            thread t2 = thread();
            if (t2.start(worker_auto_join, &auto_flag, &err) != 0)
                failures++;
            t2.drop();
        }
        if (auto_flag != 99)
            failures++;

        g_sync_cv.drop();
        g_sync_m.drop();
    }
    print("thread-ok", failures == 0 ? 1 : 0);

    stdlib_telemetry_snapshot(&tm);
    stdlib_set_error_callback(NULL);
    if (tm.alloc_count == 0 || tm.free_count == 0 || cb_calls == 0)
        failures++;
    print("telemetry-ok", failures == 0 ? 1 : 0, cb_last_cat >= 0 ? 1 : 0);

    return failures == 0 ? 0 : 1;
}
