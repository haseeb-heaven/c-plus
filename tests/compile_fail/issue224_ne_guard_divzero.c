/* `d != 0` is not the same guard as `d == 0`: when the early return is not
 * taken d is zero. Treating != as == used to mark d non-zero and hide the
 * division by zero below. */
int f(int d) {
    if (d != 0) return 0;
    return 100 / d;
}

int main(void) { return f(0); }
