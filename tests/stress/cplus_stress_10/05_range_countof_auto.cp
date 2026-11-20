#include "cplus/cplus.h"

int main(void) {
    int primes[] = {2, 3, 5, 7, 11, 13};
    auto sum = 0;
    for (auto prime : primes) {
        sum += prime;
    }

    print("RANGE", primes.len, countof(primes), sum);
    return 0;
}
