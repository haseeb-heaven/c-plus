/* EXPECT: RUNTIME (requested REJECT; stale-key use is not statically seen).
 *
 * Reason: strmap copies keys into owned storage, so the map itself survives
 * the release of the caller's buffer.  However `key` is dangling after
 * stdlib_release(), and passing it to strmap::get()/strmap::put() reaches
 * stdlib_map_hash_text()/strcmp(), i.e. a genuine use-after-free of the
 * caller's buffer.  C+ only diagnoses a released pointer when it is used in
 * the two forms it tracks directly: `p.len` or an explicit
 * cplus_check_live(p).  Passing a released pointer as an ordinary function
 * argument is not analysed, so no [CPLUS- diagnostic is emitted here.
 * (Probe: replacing the get() below with cplus_check_live(key) DOES produce
 * "[CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free", proving the
 * checker can see the stale pointer only through its explicit liveness API.)
 *
 * Exact observed outcome: check and strict build both SUCCEED with no
 * [CPLUS- diagnostic.  The runtime returns -1 from get() only because the
 * hash of recycled memory happens not to match; the stale read itself is
 * silent.  Cover this pattern in a runtime/behavioral test.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/collections.hp"

int main(void) {
    strmap sm = strmap();
    char *key = (char *)stdlib_alloc(16);
    if (key == NULL)
        return 1;
    key[0] = 'a';
    key[1] = 'b';
    key[2] = '\0';

    sm.put(key, 42);      /* map takes its own copy of the key */
    stdlib_release(key);  /* caller's buffer is now dangling */

    int out = -1;
    int rc = sm.get(key, &out); /* dangling key: UAF inside strmap::get */
    print("dangling-key", "get", rc, "out", out);

    sm.drop(); /* owned copy is released here; caller's buffer is not */
    return 0;
}
