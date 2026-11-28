/* tests/c_standards/c99/c94_features.c
 *
 * A minimal, genuine C94 (ISO/IEC 9899:1990 with Amendment 1, i.e.
 * -std=iso9899:199409) program exercising the two notable Amendment 1
 * additions:
 *
 *   1. digraphs:  <%  %>  <:  :>  (alternative tokens for { } [ ])
 *   2. wide characters: wchar_t, L'...' wide char literals, <wchar.h>
 *
 * It deliberately uses only C94 constructs: no // comments, no mixed
 * declarations, no <stdint.h>, no for-loop declarations.  Consequently it
 * must compile cleanly under -std=iso9899:199409 -pedantic-errors while the
 * C99-only source in this directory must be rejected under that same mode.
 *
 * The program is self-contained and produces deterministic stdout.
 */
#include <stdio.h>
#include <wchar.h>

int main(void)
<% /* digraph: '{' */
  wchar_t wc = L'A';           /* Amendment 1 wide character literal */
  int a<:3:> = {1, 2, 3};      /* digraphs: '[' and ']' */
  int i;

  printf("C94 wc=%d a0=%d a2=%d\n", (int)wc, a<:0:>, a<:2:>);
  for (i = 0; i < 3; ++i) {
    a<:i:> += 1;
  }
  printf("C94 sum=%d\n", a<:0:> + a<:1:> + a<:2:>);
  return 0;
%> /* digraph: '}' */
