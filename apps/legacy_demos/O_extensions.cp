/* Demo O: recommended modern extensions .cp and .hp.
 *
 * The .hp header is lowered to plain C automatically; .c/.h remain fully
 * supported for ordinary C and legacy code. */
#include "O_extensions.hp"

int main(void) {
    auto counter = Counter(10);
    counter.add(5);
    print("counter", counter.get());
    return 0;
}
