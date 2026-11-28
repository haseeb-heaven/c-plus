/* Deterministic fuzz harness for the modern C+ stdlib .hp APIs.
 *
 * This is a STANDALONE program (it has its own main).  It drives the owned
 * containers from string.hp, vector.hp and collections.hp, the recoverable
 * readers from io.hp and the error/allocator surface from error.hp with a
 * fixed-seed LCG.  Every generated operation is a *valid* operation: lengths,
 * capacities, indices and keys are bounded before they reach an API, and only
 * documented recoverable outcomes (e.g. remove of a missing key, field() past
 * the last field, EOF from a reader) are exercised.  No undefined-behaviour
 * input is generated, so a crash here is a real defect, not "fuzzing found a
 * bug in a contract-violating caller".
 *
 * Self-checking:
 *   - After every operation check_invariants() compares each live container
 *     with an independent shadow model.  A mismatch prints a labelled line
 *     and the process returns 1.
 *   - A running FNV-1a digest folds in every operation and its outcome.  The
 *     final "hp_fuzz_digest <hex>" line must be identical on every run and in
 *     every build mode (default, --strict --sanitize=none, UBSan).
 *   - On success the last line is "hp_fuzz: <N> cases ok" and exit code is 0.
 *
 * Termination ("no crash / no hang") is proven by running the binary under an
 * external timeout; see docs/stdlib/HP_FUZZ_NOTES.md.  Fatal runtime failures
 * are deliberately NOT induced in this process - the notes describe the
 * subprocess discipline for those cases.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"
#include "stdlib/io.hp"

#define CASES 1200        /* > 1000 valid operations */
#define KEYN 32           /* key/value domain: 0 .. KEYN-1 */
#define MAXSEQ 48         /* bounded container extent */
#define MAXSTR 64         /* bounded string content length */
#define NOPS 18           /* two operations per container family */

static const char ALPHA[] = "abcdefghijklmnopqrstuvwxyz0123456789,.-";

/* ---- fixed-seed LCG ----------------------------------------------------
 * Same recurrence as the deep collision test; fully reproducible. */
static unsigned long long rng_state = 0xC0FFEEULL;

static unsigned int rnd(void) {
    rng_state = rng_state * 6364136223846793005ULL +
                1442695040888963407ULL;
    return (unsigned int)(rng_state >> 33);
}

#define FNV_PRIME 1099511628211ULL

static unsigned long long mix(unsigned long long d, unsigned long long v) {
    return (d ^ v) * FNV_PRIME;
}

/* ---- live containers (zero-initialised == valid empty containers) ------ */
static string g_str;
static vector g_vec;
static intmap g_im;
static intset g_is;
static strmap g_sm;
static deque g_dq;
static list g_lst;
static stack g_stk;
static queue g_q;

/* ---- independent shadow models ---------------------------------------- */
static char s_shadow[MAXSTR + 1];
static size_t s_n = 0;
static int v_shadow[MAXSEQ];
static size_t v_n = 0;
static unsigned char im_has[KEYN];
static int im_val[KEYN];
static size_t im_n = 0;
static unsigned char is_has[KEYN];
static size_t is_n = 0;
static unsigned char sm_has[KEYN];
static int sm_val[KEYN];
static size_t sm_n = 0;
static int dq_shadow[MAXSEQ];
static size_t dq_n = 0;
static int lst_shadow[MAXSEQ];
static size_t lst_n = 0;
static int stk_shadow[MAXSEQ];
static size_t stk_n = 0;
static int q_shadow[MAXSEQ];
static size_t q_n = 0;

static int bad(const char *label) {
    print("hp_fuzz: INVARIANT", label);
    return 0;
}

/* Same diagnostic as bad(), but returns 1 so a driver can "return fail_here".
 * Keeps the main loop free of the comma operator. */
static int fail_here(const char *label) {
    print("hp_fuzz: INVARIANT", label);
    return 1;
}

/* Compare every live container against its shadow.  Returns 1 when all
 * invariants hold; prints the first violation and returns 0 otherwise. */
static int check_invariants(void) {
    size_t i;
    int out = 0;
    slice view;
    string_view text_view;
    char key[16];

    if (!g_str.valid())
        return bad("string.valid");
    if (g_str.size() != s_n)
        return bad("string.size");
    if (g_str.capacity() < g_str.size())
        return bad("string.capacity");
    if (strlen(g_str.c_str()) != s_n)
        return bad("string.strlen");
    if (s_n > 0 && memcmp(g_str.c_str(), s_shadow, s_n) != 0)
        return bad("string.contents");
    if (!g_str.is_valid_utf8())
        return bad("string.utf8");
    text_view = g_str.to_view();
    if (!text_view.equals(s_shadow))
        return bad("string.view");

    if (g_vec.size() != v_n)
        return bad("vector.size");
    if (g_vec.capacity() < g_vec.size())
        return bad("vector.capacity");
    for (i = 0; i < v_n; i++)
        if (g_vec.at_checked(i, &out) != 0 || out != v_shadow[i])
            return bad("vector.at");
    view = g_vec.view();
    if (view.size() != v_n || view.stale())
        return bad("vector.view");
    for (i = 0; i < v_n; i++)
        if (view.at_checked(i, &out) != 0 || out != v_shadow[i])
            return bad("vector.view.at");

    if (!g_im.valid() || g_im.size() != im_n)
        return bad("intmap.size");
    for (i = 0; i < KEYN; i++) {
        int present = g_im.contains((int)i);
        if (present != (int)im_has[i])
            return bad("intmap.contains");
        if (present) {
            if (g_im.get((int)i, &out) != 0 || out != im_val[i])
                return bad("intmap.get");
        }
    }

    if (!g_is.valid() || g_is.size() != is_n)
        return bad("intset.size");
    for (i = 0; i < KEYN; i++)
        if (g_is.contains((int)i) != (int)is_has[i])
            return bad("intset.contains");

    if (!g_sm.valid() || g_sm.size() != sm_n)
        return bad("strmap.size");
    for (i = 0; i < KEYN; i++) {
        int present;
        snprintf(key, sizeof(key), "s%u", (unsigned int)i);
        present = g_sm.contains(key);
        if (present != (int)sm_has[i])
            return bad("strmap.contains");
        if (present) {
            if (g_sm.get(key, &out) != 0 || out != sm_val[i])
                return bad("strmap.get");
        }
    }

    if (g_dq.size() != dq_n)
        return bad("deque.size");
    for (i = 0; i < dq_n; i++)
        if (g_dq.at_checked(i, &out) != 0 || out != dq_shadow[i])
            return bad("deque.at");

    if (g_lst.size() != lst_n)
        return bad("list.size");
    for (i = 0; i < lst_n; i++)
        if (g_lst.at_checked(i, &out) != 0 || out != lst_shadow[i])
            return bad("list.at");

    if (g_stk.size() != stk_n)
        return bad("stack.size");
    if (stk_n > 0 && (g_stk.top_checked(&out) != 0 ||
                      out != stk_shadow[stk_n - 1]))
        return bad("stack.top");

    if (g_q.size() != q_n)
        return bad("queue.size");
    if (q_n > 0 && (g_q.front_checked(&out) != 0 || out != q_shadow[0]))
        return bad("queue.front");

    return 1;
}

/* io.hp: drive the readers on an empty stream.  EOF is a documented,
 * non-failure outcome; the error value must stay OK.  stdin is reopened to
 * /dev/null inside the process so the harness is independent of how a caller
 * redirects standard input. */
static int io_check(void) {
    char buf[32];
    int iv = 12345;
    long lv = 12345;
    double dv = 1.5;
    error err = error();

    (void)freopen("/dev/null", "r", stdin);
    if (read_int(&iv, &err) != STDLIB_EOF || !err.ok()) {
        print("hp_fuzz: BAD io.read_int");
        return 0;
    }
    if (stdlib_io_token(buf, sizeof(buf)) != 1) {
        print("hp_fuzz: BAD io.token");
        return 0;
    }
    if (read_line(buf, sizeof(buf), &err) != STDLIB_EOF || !err.ok()) {
        print("hp_fuzz: BAD io.read_line");
        return 0;
    }
    if (read_long(&lv, &err) != STDLIB_EOF || !err.ok()) {
        print("hp_fuzz: BAD io.read_long");
        return 0;
    }
    if (read_double(&dv, &err) != STDLIB_EOF || !err.ok()) {
        print("hp_fuzz: BAD io.read_double");
        return 0;
    }
    err.clear();
    if (!err.ok() || err.category() != STDLIB_CATEGORY_OK ||
        err.code() != (int)CPLUS_EXIT_OK) {
        print("hp_fuzz: BAD error.clear");
        return 0;
    }
    return 1;
}

/* Build a short NUL-terminated piece from a bounded alphabet. */
static size_t make_piece(unsigned int r, char *out, size_t cap) {
    size_t n = 1 + (size_t)(r % 4u);
    size_t i;
    if (n + 1 > cap)
        n = cap - 1;
    for (i = 0; i < n; i++)
        out[i] = ALPHA[(r >> (8u + 4u * (unsigned int)i)) %
                       (sizeof(ALPHA) - 1u)];
    out[n] = '\0';
    return n;
}

int main(void) {
    unsigned long long digest = 1469598103934665603ULL;
    int i;
    int cases = 0;
    char piece[8];
    char tmp[32];
    char hex[24];
    size_t plen;

    /* Seed the string and make the hash maps valid, all through public API. */
    if (g_str.append("seed") != 0)
        return fail_here("init.string");
    s_n = 4;
    memcpy(s_shadow, "seed", 4);
    s_shadow[s_n] = '\0';
    if (g_im.grow(8) != 0 || g_is.grow(8) != 0 || g_sm.grow(8) != 0)
        return fail_here("init.maps");
    if (!io_check())
        return 1;
    if (!check_invariants())
        return 1;

    for (i = 0; i < CASES; i++) {
        unsigned int r0 = rnd();
        unsigned int r1 = rnd();
        unsigned int r2 = rnd();
        int kind = (int)(r0 % (unsigned int)NOPS);
        int a = (int)(r1 % (unsigned int)KEYN);
        int b = (int)(r2 % 64u);
        int rc;
        int out = 0;
        size_t at;
        char key[16];

        /* Fold the raw (bounded) inputs so the digest covers the schedule. */
        digest = mix(digest, (unsigned long long)i);
        digest = mix(digest, (unsigned long long)kind);
        digest = mix(digest, (unsigned long long)a);
        digest = mix(digest, (unsigned long long)b);

        switch (kind) {
        case 0: /* string: bounded append, clear when full */
            plen = make_piece(r1, piece, sizeof(piece));
            if (s_n + plen > MAXSTR) {
                g_str.clear();
                s_n = 0;
                s_shadow[0] = '\0';
            }
            if (g_str.append(piece) != 0)
                return fail_here("string.append");
            memcpy(s_shadow + s_n, piece, plen);
            s_n += plen;
            s_shadow[s_n] = '\0';
            break;

        case 1: /* string: reserve + bounded checked reads */
            if (g_str.reserve((size_t)(r1 % 48u)) != 0)
                return fail_here("string.reserve");
            if (g_str.substr(0, (size_t)(r1 % 8u), tmp, sizeof(tmp)) != 0)
                return fail_here("string.substr");
            rc = g_str.field(',', (size_t)(r1 % 4u), tmp, sizeof(tmp));
            digest = mix(digest, (unsigned long long)(rc + 2));
            digest = mix(digest, (unsigned long long)(unsigned char)
                         g_str.at((size_t)(r1 % (s_n + 1u))));
            digest = mix(digest, (unsigned long long)(g_str.find("a") + 2));
            break;

        case 2: /* vector: push_back or bounded insert */
            if (v_n >= MAXSEQ) {
                g_vec.clear();
                v_n = 0;
            }
            if ((r1 & 1u) == 0) {
                if (g_vec.push_back(b) != 0)
                    return fail_here("vector.push_back");
                v_shadow[v_n++] = b;
            } else {
                at = (size_t)(r1 % (v_n + 1u));
                if (g_vec.insert(at, b) != 0)
                    return fail_here("vector.insert");
                memmove(v_shadow + at + 1, v_shadow + at,
                        (v_n - at) * sizeof(int));
                v_shadow[at] = b;
                v_n++;
            }
            break;

        case 3: /* vector: erase / pop / checked read */
            if (v_n > 0) {
                at = (size_t)(r1 % v_n);
                if ((r1 & 1u) == 0) {
                    if (g_vec.erase(at, &out) != 0 || out != v_shadow[at])
                        return fail_here("vector.erase");
                    memmove(v_shadow + at, v_shadow + at + 1,
                            (v_n - at - 1) * sizeof(int));
                    v_n--;
                } else {
                    if (g_vec.pop_back(&out) != 0 || out != v_shadow[v_n - 1])
                        return fail_here("vector.pop_back");
                    v_n--;
                }
            }
            rc = g_vec.at_checked((size_t)(r1 % (v_n + 1u)), &out);
            digest = mix(digest, (unsigned long long)(rc + 2));
            break;

        case 4: /* intmap: bounded put */
            if (im_n >= KEYN - 2) {
                g_im.clear();
                im_n = 0;
                memset(im_has, 0, sizeof(im_has));
            }
            if (g_im.put(a, b) != 0)
                return fail_here("intmap.put");
            if (!im_has[a]) {
                im_has[a] = 1;
                im_n++;
            }
            im_val[a] = b;
            break;

        case 5: /* intmap: remove / get (absent key is a documented -1) */
            if ((r1 & 1u) == 0) {
                rc = g_im.remove(a);
                if (im_has[a]) {
                    if (rc != 0)
                        return fail_here("intmap.remove");
                    im_has[a] = 0;
                    im_n--;
                } else if (rc != -1) {
                    return fail_here("intmap.remove.absent");
                }
            } else {
                rc = g_im.get(a, &out);
                if (im_has[a]) {
                    if (rc != 0 || out != im_val[a])
                        return fail_here("intmap.get");
                } else if (rc != -1) {
                    return fail_here("intmap.get.absent");
                }
            }
            break;

        case 6: /* intset: bounded insert */
            if (is_n >= KEYN - 2) {
                g_is.clear();
                is_n = 0;
                memset(is_has, 0, sizeof(is_has));
            }
            if (g_is.insert(a) != 0)
                return fail_here("intset.insert");
            if (!is_has[a]) {
                is_has[a] = 1;
                is_n++;
            }
            break;

        case 7: /* intset: remove / contains */
            if ((r1 & 1u) == 0) {
                rc = g_is.remove(a);
                if (is_has[a]) {
                    if (rc != 0)
                        return fail_here("intset.remove");
                    is_has[a] = 0;
                    is_n--;
                } else if (rc != -1) {
                    return fail_here("intset.remove.absent");
                }
            } else if (g_is.contains(a) != (int)is_has[a]) {
                return fail_here("intset.contains");
            }
            break;

        case 8: /* strmap: bounded put with a short owned key */
            if (sm_n >= KEYN - 2) {
                g_sm.clear();
                sm_n = 0;
                memset(sm_has, 0, sizeof(sm_has));
            }
            snprintf(key, sizeof(key), "s%d", a);
            if (g_sm.put(key, b) != 0)
                return fail_here("strmap.put");
            if (!sm_has[a]) {
                sm_has[a] = 1;
                sm_n++;
            }
            sm_val[a] = b;
            break;

        case 9: /* strmap: remove / get */
            snprintf(key, sizeof(key), "s%d", a);
            if ((r1 & 1u) == 0) {
                rc = g_sm.remove(key);
                if (sm_has[a]) {
                    if (rc != 0)
                        return fail_here("strmap.remove");
                    sm_has[a] = 0;
                    sm_n--;
                } else if (rc != -1) {
                    return fail_here("strmap.remove.absent");
                }
            } else {
                rc = g_sm.get(key, &out);
                if (sm_has[a]) {
                    if (rc != 0 || out != sm_val[a])
                        return fail_here("strmap.get");
                } else if (rc != -1) {
                    return fail_here("strmap.get.absent");
                }
            }
            break;

        case 10: /* deque: bounded push_back / push_front */
            if (dq_n >= MAXSEQ) {
                g_dq.clear();
                dq_n = 0;
            }
            if ((r1 & 1u) == 0) {
                if (g_dq.push_back(b) != 0)
                    return fail_here("deque.push_back");
                dq_shadow[dq_n++] = b;
            } else {
                if (g_dq.push_front(b) != 0)
                    return fail_here("deque.push_front");
                memmove(dq_shadow + 1, dq_shadow, dq_n * sizeof(int));
                dq_shadow[0] = b;
                dq_n++;
            }
            break;

        case 11: /* deque: pop / checked read */
            if (dq_n > 0) {
                if ((r1 & 1u) == 0) {
                    if (g_dq.pop_front(&out) != 0 || out != dq_shadow[0])
                        return fail_here("deque.pop_front");
                    memmove(dq_shadow, dq_shadow + 1,
                            (dq_n - 1) * sizeof(int));
                    dq_n--;
                } else {
                    if (g_dq.pop_back(&out) != 0 ||
                        out != dq_shadow[dq_n - 1])
                        return fail_here("deque.pop_back");
                    dq_n--;
                }
            }
            if (dq_n > 0) {
                if (g_dq.at_checked((size_t)(r1 % dq_n), &out) != 0)
                    return fail_here("deque.at");
                digest = mix(digest, (unsigned long long)out);
            }
            break;

        case 12: /* list: bounded push_back / push_front */
            if (lst_n >= MAXSEQ) {
                g_lst.clear();
                lst_n = 0;
            }
            if ((r1 & 1u) == 0) {
                if (g_lst.push_back(b) != 0)
                    return fail_here("list.push_back");
                lst_shadow[lst_n++] = b;
            } else {
                if (g_lst.push_front(b) != 0)
                    return fail_here("list.push_front");
                memmove(lst_shadow + 1, lst_shadow, lst_n * sizeof(int));
                lst_shadow[0] = b;
                lst_n++;
            }
            break;

        case 13: /* list: pop / checked read */
            if (lst_n > 0) {
                if ((r1 & 1u) == 0) {
                    if (g_lst.pop_front(&out) != 0 || out != lst_shadow[0])
                        return fail_here("list.pop_front");
                    memmove(lst_shadow, lst_shadow + 1,
                            (lst_n - 1) * sizeof(int));
                    lst_n--;
                } else {
                    if (g_lst.pop_back(&out) != 0 ||
                        out != lst_shadow[lst_n - 1])
                        return fail_here("list.pop_back");
                    lst_n--;
                }
            }
            if (lst_n > 0) {
                if (g_lst.at_checked((size_t)(r1 % lst_n), &out) != 0)
                    return fail_here("list.at");
                digest = mix(digest, (unsigned long long)out);
            }
            break;

        case 14: /* stack: bounded push */
            if (stk_n >= MAXSEQ) {
                g_stk.clear();
                stk_n = 0;
            }
            if (g_stk.push(b) != 0)
                return fail_here("stack.push");
            stk_shadow[stk_n++] = b;
            break;

        case 15: /* stack: pop / top */
            if (stk_n > 0) {
                if ((r1 & 1u) == 0) {
                    if (g_stk.pop(&out) != 0 || out != stk_shadow[stk_n - 1])
                        return fail_here("stack.pop");
                    stk_n--;
                } else {
                    if (g_stk.top_checked(&out) != 0 ||
                        out != stk_shadow[stk_n - 1])
                        return fail_here("stack.top");
                    digest = mix(digest, (unsigned long long)out);
                }
            }
            break;

        case 16: /* queue: bounded push */
            if (q_n >= MAXSEQ) {
                g_q.clear();
                q_n = 0;
            }
            if (g_q.push(b) != 0)
                return fail_here("queue.push");
            q_shadow[q_n++] = b;
            break;

        default: /* queue: pop / front */
            if (q_n > 0) {
                if ((r1 & 1u) == 0) {
                    if (g_q.pop(&out) != 0 || out != q_shadow[0])
                        return fail_here("queue.pop");
                    memmove(q_shadow, q_shadow + 1,
                            (q_n - 1) * sizeof(int));
                    q_n--;
                } else {
                    if (g_q.front_checked(&out) != 0 || out != q_shadow[0])
                        return fail_here("queue.front");
                    digest = mix(digest, (unsigned long long)out);
                }
            }
            break;
        }

        /* Fold the bounded post-operation extents into the digest. */
        digest = mix(digest, (unsigned long long)s_n);
        digest = mix(digest, (unsigned long long)v_n);
        digest = mix(digest, (unsigned long long)im_n);
        digest = mix(digest, (unsigned long long)is_n);
        digest = mix(digest, (unsigned long long)sm_n);
        digest = mix(digest, (unsigned long long)dq_n);
        digest = mix(digest, (unsigned long long)lst_n);
        digest = mix(digest, (unsigned long long)stk_n);
        digest = mix(digest, (unsigned long long)q_n);

        if (!check_invariants())
            return 1;
        cases++;
    }

    /* Release every container exactly once; drop() is idempotent. */
    g_str.drop();
    g_vec.drop();
    g_im.drop();
    g_is.drop();
    g_sm.drop();
    g_dq.drop();
    g_lst.drop();
    g_stk.drop();
    g_q.drop();

    snprintf(hex, sizeof(hex), "%016llx", digest);
    print("hp_fuzz_digest", hex);
    print("hp_fuzz:", cases, "cases ok");
    return 0;
}
