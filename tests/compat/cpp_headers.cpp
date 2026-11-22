// C++ consumer compatibility check for the same nine ISO C headers.
// Verifies that the installed C headers remain usable from C++ with no
// source-tree paths and no C++-specific workarounds beyond extern "C".
#include <cassert>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main() {
    char buf[32];
    int n = std::snprintf(buf, sizeof(buf), "%d/%d", INT_MAX, UINT8_MAX);
    assert(n > 0 && static_cast<size_t>(n) < sizeof(buf));
    assert(std::strlen(buf) == static_cast<size_t>(n));
    assert(std::isalpha('a') && !std::isalpha('0'));
    assert(std::toupper('z') == 'Z');
    errno = 0;
    (void)std::strtol("123", nullptr, 10);
    assert(errno == 0);
    {
        void *p = std::malloc(16);
        assert(p != nullptr);
        std::memset(p, 0x5a, 16);
        std::free(p);
    }
    std::puts("compat-cpp: ok");
    return 0;
}
