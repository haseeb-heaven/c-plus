/* Standalone C99 test for the platform-backed cplus_wctrans / cplus_towctrans wrappers. */
#include <assert.h>
#include <wctype.h>
#include <errno.h>
#include <locale.h>

extern wctrans_t cplus_wctrans(const char *property);
extern wint_t cplus_towctrans(wint_t wc, wctrans_t desc);

int main(void) {
    setlocale(LC_ALL, "C");

    /* Both standard extensible mappings yield nonzero descriptors in the C locale. */
    wctrans_t upper = cplus_wctrans("toupper");
    wctrans_t lower = cplus_wctrans("tolower");
    assert(upper != (wctrans_t)0);
    assert(lower != (wctrans_t)0);

    /* towctrans applies the mapping described by the descriptor. */
    assert(cplus_towctrans(L'a', upper) == L'A');
    assert(cplus_towctrans(L'A', lower) == L'a');

    /* Applying a mapping that cannot change the input leaves it unchanged
       (towupper('A') == 'A', towlower('a') == 'a'). */
    assert(cplus_towctrans(L'A', upper) == L'A');
    assert(cplus_towctrans(L'a', lower) == L'a');

    /* NULL property is guarded with EINVAL and a zero descriptor. */
    errno = 0;
    assert(cplus_wctrans(NULL) == (wctrans_t)0 && errno == EINVAL);

    /* C17 requires wctrans to return 0 for an unsupported property name in the
       current locale. This host (macOS libc) returns 0; documented as host behavior. */
    assert(cplus_wctrans("__definitely_not_a_property__") == (wctrans_t)0);

    return 0;
}
