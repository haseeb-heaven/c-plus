/* Adversarial lifecycle regression family: repeated create/drop cycles,
 * inert double-drop, and use-after-drop STATE QUERIES ONLY.
 *
 * Scope (all bounded, all deterministic):
 *   - 100 cycles each: string, vector, intset/intmap/strmap/deque/list/
 *     stack/queue, file (tmpfile) and the thread.hp guard/mutex/thread handles.
 *   - double drop() on every family is inert (no fatal, no telemetry movement).
 *   - after drop(), only state-query methods are called (size/capacity/valid/
 *     is_open/holds/is_valid/is_joinable/equals/compare/find/is_valid_utf8/
 *     c_str-as-pointer).  No stale pointer is ever dereferenced and no
 *     element/IO accessor is called on a dropped object.
 *
 * METHOD.  A fixed-seed xorshift32 (`adv_rnd`) drives payload lengths/values so
 * every run is byte-identical.  Each cycle is folded into one `<tag> <1|0>`
 * self-check; the final line is `ADV_LIFECYCLE <checks> <failures>`.  Exit code
 * is 1 when any check fails.  Telemetry and open-fd counts are sampled across
 * the whole run to catch leaks that a single cycle would hide.
 *
 * NOTE ON C+ LOWERING: owning locals use unique names inside a function (the
 * RaiiLowering pass de-duplicates ownership by variable name), and every owner
 * is also dropped explicitly so a redundant implicit destructor is harmless.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"
#include "stdlib/file.hp"
#include "stdlib/thread.hp"

#define ADV_CYCLES 100

static int adv_checks = 0;
static int adv_failed = 0;
static unsigned int adv_seed = 0xC0FFEEu;
static int adv_thr_count = 0;

static unsigned int adv_rnd(void) {
    adv_seed ^= adv_seed << 13;
    adv_seed ^= adv_seed >> 17;
    adv_seed ^= adv_seed << 5;
    return adv_seed;
}

static void adv_ck(const char *tag, int cond) {
    adv_checks++;
    if (!cond)
        adv_failed++;
    print(tag, cond ? 1 : 0);
}

static int adv_fd_count(void) {
    int n = 0;
    int fd;
    for (fd = 0; fd < 256; fd++)
        if (fcntl(fd, F_GETFD) != -1)
            n++;
    return n;
}

static void *adv_thr_worker(void *arg) {
    (void)arg;
    adv_thr_count++;
    return NULL;
}

/* ---- string -------------------------------------------------------------- */
static void adv_string_cycles(void) {
    int i, n;
    char txt[16];
    char lbl[32];
    for (i = 0; i < ADV_CYCLES; i++) {
        int ok;
        n = 1 + (int)(adv_rnd() % 7u);
        memset(txt, (int)('a' + (adv_rnd() % 26u)), (size_t)n);
        txt[n] = '\0';
        {
            string adv_s = string(txt);
            ok = adv_s.valid() == 1 && adv_s.size() == (size_t)n &&
                 adv_s.capacity() == (size_t)(n + 1) &&
                 adv_s.equals(txt) == 1 && adv_s.is_valid_utf8() == 1;
            adv_s.drop();
            ok = ok && adv_s.valid() == 0 && adv_s.size() == 0 &&
                 adv_s.capacity() == 0;
            /* inert double drop */
            adv_s.drop();
            ok = ok && adv_s.valid() == 0 && adv_s.size() == 0 &&
                 adv_s.capacity() == 0;
            /* use-after-drop: state queries only, no stale dereference */
            ok = ok && adv_s.equals("") == 1 && adv_s.compare("") == 0 &&
                 adv_s.find("x") == -1 && adv_s.is_valid_utf8() == 1 &&
                 adv_s.c_str() != NULL;
        }
        snprintf(lbl, sizeof(lbl), "lc_str_%03d", i);
        adv_ck(lbl, ok);
    }
}

/* ---- vector -------------------------------------------------------------- */
static void adv_vector_cycles(void) {
    int i, j, n, v = 0;
    int exp[8];
    char lbl[32];
    for (i = 0; i < ADV_CYCLES; i++) {
        int ok = 1;
        n = 1 + (int)(adv_rnd() % 8u);
        for (j = 0; j < n; j++) {
            exp[j] = (int)(adv_rnd() % 1000u);
        }
        {
            vector adv_v = vector();
            for (j = 0; j < n; j++)
                if (adv_v.push_back(exp[j]) != 0)
                    ok = 0;
            if (adv_v.size() != (size_t)n)
                ok = 0;
            for (j = 0; j < n; j++)
                if (adv_v.at_checked((size_t)j, &v) != 0 || v != exp[j])
                    ok = 0;
            adv_v.drop();
            ok = ok && adv_v.size() == 0 && adv_v.capacity() == 0;
            adv_v.drop();
            ok = ok && adv_v.size() == 0 && adv_v.capacity() == 0;
        }
        snprintf(lbl, sizeof(lbl), "lc_vec_%03d", i);
        adv_ck(lbl, ok);
    }
}

/* ---- collections --------------------------------------------------------- */
static void adv_collections_cycles(void) {
    int i, out = 0;
    char lbl[32];
    for (i = 0; i < ADV_CYCLES; i++) {
        int ok = 1;
        {
            intset adv_set = intset();
            intmap adv_map = intmap();
            strmap adv_smap = strmap();
            deque adv_deq = deque();
            list adv_lst = list();
            stack adv_stk = stack();
            queue adv_que = queue();

            if (adv_set.insert(10) != 0 || adv_set.insert(20) != 0 ||
                adv_set.insert(30) != 0 || !adv_set.contains(20))
                ok = 0;
            if (adv_map.put(10, 11) != 0 || adv_map.get(10, &out) != 0 || out != 11)
                ok = 0;
            if (adv_smap.put("k1", 12) != 0 ||
                adv_smap.get("k1", &out) != 0 || out != 12)
                ok = 0;
            if (adv_deq.push_back(7) != 0 || adv_deq.push_front(8) != 0 ||
                adv_deq.at_checked(0, &out) != 0 || out != 8)
                ok = 0;
            if (adv_lst.push_back(5) != 0 || adv_lst.push_front(6) != 0 ||
                adv_lst.at_checked(0, &out) != 0 || out != 6)
                ok = 0;
            if (adv_stk.push(3) != 0 || adv_stk.top_checked(&out) != 0 || out != 3)
                ok = 0;
            if (adv_que.push(4) != 0 || adv_que.front_checked(&out) != 0 || out != 4)
                ok = 0;

            if (adv_set.size() != 3 || adv_set.valid() != 1 ||
                adv_map.size() != 1 || adv_map.valid() != 1 ||
                adv_smap.size() != 1 || adv_smap.valid() != 1 ||
                adv_deq.size() != 2 || adv_lst.size() != 2 ||
                adv_stk.size() != 1 || adv_que.size() != 1)
                ok = 0;

            /* drop all, then drop all again: both must be inert */
            adv_set.drop(); adv_map.drop(); adv_smap.drop(); adv_deq.drop();
            adv_lst.drop(); adv_stk.drop(); adv_que.drop();
            adv_set.drop(); adv_map.drop(); adv_smap.drop(); adv_deq.drop();
            adv_lst.drop(); adv_stk.drop(); adv_que.drop();

            /* use-after-drop: state queries only */
            if (adv_set.size() != 0 || adv_set.valid() != 0 ||
                adv_map.size() != 0 || adv_map.valid() != 0 ||
                adv_smap.size() != 0 || adv_smap.valid() != 0 ||
                adv_deq.size() != 0 || adv_lst.size() != 0 ||
                adv_stk.size() != 0 || adv_que.size() != 0)
                ok = 0;
        }
        snprintf(lbl, sizeof(lbl), "lc_col_%03d", i);
        adv_ck(lbl, ok);
    }
}

/* ---- file ---------------------------------------------------------------- */
static void adv_file_cycles(void) {
    int i;
    char lbl[32];
    char buf[4];
    int fd_before = adv_fd_count();

    for (i = 0; i < ADV_CYCLES; i++) {
        int ok = 1;
        {
            error adv_fe = error();
            file adv_f = file_temp(&adv_fe);
            if (!adv_f.is_open() || !adv_fe.ok())
                ok = 0;
            if (adv_f.write_bytes("abc", 3, &adv_fe) != STDLIB_OK)
                ok = 0;
            if (adv_f.seek(0, SEEK_SET, &adv_fe) != STDLIB_OK)
                ok = 0;
            buf[0] = buf[1] = buf[2] = buf[3] = '\0';
            if (adv_f.read(buf, 3, &adv_fe) != STDLIB_OK ||
                buf[0] != 'a' || buf[1] != 'b' || buf[2] != 'c')
                ok = 0;
            adv_f.drop();
            if (adv_f.is_open())
                ok = 0;
            adv_f.drop(); /* inert double drop */
            if (adv_f.is_open())
                ok = 0;
            /* state query after drop: close reports lifecycle failure */
            if (adv_f.close(&adv_fe) != STDLIB_FAIL ||
                adv_fe.category() != STDLIB_CATEGORY_LIFECYCLE)
                ok = 0;
        }
        snprintf(lbl, sizeof(lbl), "lc_file_%03d", i);
        adv_ck(lbl, ok);
    }

    {
        /* invalid handle: every operation is a recoverable failure */
        error adv_be = error();
        file adv_bad = file(NULL, NULL);
        char rb[2];
        int ok = adv_bad.is_open() == 0;
        if (adv_bad.read(rb, 1, &adv_be) != STDLIB_FAIL ||
            adv_be.category() != STDLIB_CATEGORY_IO)
            ok = 0;
        if (adv_bad.write("x") != 0)
            ok = 0;
        if (adv_bad.close(&adv_be) != STDLIB_FAIL ||
            adv_be.category() != STDLIB_CATEGORY_LIFECYCLE)
            ok = 0;
        adv_bad.drop();
        adv_bad.drop();
        ok = ok && adv_bad.is_open() == 0;
        adv_ck("lc_file_badhandle", ok);
    }
    adv_ck("lc_file_nofdleak", adv_fd_count() == fd_before);
}

/* ---- thread.hp guard / mutex / thread ----------------------------------- */
static void adv_guard_cycles(void) {
    int i;
    char lbl[32];
    for (i = 0; i < ADV_CYCLES; i++) {
        int ok;
        {
            mutex adv_gm = mutex();
            guard adv_g = guard(&adv_gm);
            ok = adv_gm.is_valid() == 1 && adv_g.holds() == 1;
            adv_g.unlock();
            ok = ok && adv_g.holds() == 0;
            adv_g.drop();
            ok = ok && adv_g.holds() == 0;
            adv_g.drop(); /* inert double drop */
            ok = ok && adv_g.holds() == 0;
            /* mutex itself is still usable, then inert double drop */
            adv_gm.lock();
            adv_gm.unlock();
            adv_gm.drop();
            ok = ok && adv_gm.is_valid() == 0;
            adv_gm.drop();
            ok = ok && adv_gm.is_valid() == 0;
        }
        snprintf(lbl, sizeof(lbl), "lc_grd_%03d", i);
        adv_ck(lbl, ok);
    }
}

static void adv_thread_cycles(void) {
    int i;
    char lbl[32];
    for (i = 0; i < ADV_CYCLES; i++) {
        int ok = 1;
        {
            error adv_te = error();
            thread adv_t = thread();
            void *adv_res = NULL;
            if (adv_t.start(adv_thr_worker, NULL, &adv_te) != STDLIB_OK ||
                !adv_te.ok() || !adv_t.is_joinable())
                ok = 0;
            if (adv_t.join(&adv_res, &adv_te) != STDLIB_OK || !adv_te.ok() ||
                adv_res != NULL || adv_t.is_joinable())
                ok = 0;
            if (adv_thr_count != i + 1)
                ok = 0;
            /* repeated join is a clean lifecycle failure */
            if (adv_t.join(NULL, &adv_te) != STDLIB_FAIL ||
                adv_te.category() != STDLIB_CATEGORY_LIFECYCLE)
                ok = 0;
            adv_t.drop();
            adv_t.drop(); /* inert double drop */
            ok = ok && adv_t.is_joinable() == 0;
        }
        snprintf(lbl, sizeof(lbl), "lc_thr_%03d", i);
        adv_ck(lbl, ok);
    }
}

int main(void) {
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();

    adv_string_cycles();
    adv_vector_cycles();
    adv_collections_cycles();
    adv_file_cycles();
    adv_guard_cycles();
    adv_thread_cycles();

    {
        stdlib_telemetry_stats st;
        stdlib_telemetry_snapshot(&st);
        adv_ck("lc_balanced", st.alloc_count == st.free_count);
    }
    stdlib_telemetry_set_enabled(0);

    print("ADV_LIFECYCLE", adv_checks, adv_failed);
    return adv_failed ? 1 : 0;
}
