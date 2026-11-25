/* C+ modern-stdlib speed workload for the safe-vs-legacy comparison.
 *
 * Exercises the owned containers from stdlib/string.hp and stdlib/vector.hp:
 *   - vector: repeated push_back (growth + checked allocator) and at() reads
 *   - string: construction, repeated append (growth) and size() reads
 *
 * This is the SAME source compiled two ways so the comparison is fair:
 *   safe   = c+ build --release                  (Mode::Safe, UBSan on)
 *   legacy = c+ build --release --legacy-c       (Mode::LegacyC, no UBSan)
 * Only the compiler safety instrumentation differs; the workload, the -O2
 * optimisation level and the stdlib layer are identical.
 *
 * Checksum: vsum = sum(0..N-1) = N*(N-1)/2, ssize_sum = sum of string sizes
 * after each of M appends. Both are integer-exact and easy to reproduce in
 * Rust and Goose, so identical output verifies semantics across variants.
 */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"

#define N 2000000
#define M 200000

int main(void) {
    long long vsum = 0;
    long long ssize_sum = 0;
    int i;

    vector v = vector();
    for (i = 0; i < N; i++) {
        if (v.push_back(i) != 0)
            return 1;
    }
    for (i = 0; i < N; i++)
        vsum += (long long)v.at((size_t)i);

    string s = string("a");
    for (i = 0; i < M; i++) {
        if (s.append("bbbbbbbbbb") != 0)
            return 1;
        ssize_sum += (long long)s.size();
    }

    printf("%lld\n", vsum + ssize_sum);
    return 0;
}
