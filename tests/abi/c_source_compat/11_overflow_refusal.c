/* Legacy 11 -- the classic local-buffer overflow (DELIBERATE).
 *
 * Ordinary code copies 40 bytes into an 8-byte stack buffer. The adjacent
 * guard[] array catches the spill so the UNCHECKED build does not crash: it
 * silently corrupts guard[0] and exits 0. The checked layer proves the
 * destination capacity is 8 and refuses the memcpy with ESNOSPC 406,
 * emitting a [CPLUS-SAFE] diagnostic on stderr and terminating with 70.
 *
 * This file is intentionally NOT a stdout/exit parity case. It is run as its
 * own subprocess to capture the exact refusal diagnostic and exit status.
 */
#include <stdio.h>
#include <string.h>

int main(void)
{
    struct {
        char buf[8];
        char guard[64];
    } s;
    char src[40];
    int i;

    memset(s.guard, 0x5A, sizeof(s.guard));
    for (i = 0; i < 40; ++i) src[i] = (char)('A' + (i % 26));

    printf("overflow_refusal: before guard0=%02X\n",
           (unsigned)(unsigned char)s.guard[0]);

    /* Plain C: writes 32 bytes past buf[] into guard[] (silent corruption).
     * Checked C: refused before any byte is written. */
    memcpy(s.buf, src, 40);

    printf("overflow_refusal: after guard0=%02X buf0=%02X\n",
           (unsigned)(unsigned char)s.guard[0],
           (unsigned)(unsigned char)s.buf[0]);
    return 0;
}
