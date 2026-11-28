/* Case E: narrowing integer conversion.
 *
 * 300 does not fit in a (signed) char.  The implicit conversion changes the
 * value, so C+ must emit an asserted diagnostic: a warning in DEFAULT mode
 * (rc 0, build succeeds) and a hard error in STRICT mode (rc != 0).
 * This is the compile-time narrowing check; runtime-only narrowing of an
 * unknown value is a documented gap (known_gaps.md) and is not claimed here.
 */
int main(void) {
  char c = 300;
  return c == 44 ? 0 : 1;
}
