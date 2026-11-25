/* Compile-time negative fixture for the end-to-end integration suite.
 *
 * The index is a compile-time constant and the array extent is known, so the
 * C+ bounds pass can prove the violation without running the program:
 *
 *     values[9]   with   int values[4]
 *
 * `c+ check tests/integration/contract_probe.c` MUST reject this file.  The
 * matching substring of the real diagnostic lives in expected_diag.txt.
 */
int main(void) {
    int values[4] = { 10, 20, 30, 40 };
    return values[9];
}
