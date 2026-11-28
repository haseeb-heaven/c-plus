/* Demo: safe interactive input with recoverable errors.
 * Run:  echo "Ada 42 1.5" | <exe>
 */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/io.hp"

int main(void) {
    error err = error();
    char name[32];
    int age = 0;
    double score = 0;

    if (read_line(name, sizeof(name), &err) != STDLIB_OK) {
        print("no name given");
        return 1;
    }
    int got_age = read_int(&age, &err);
    int got_score = read_double(&score, &err);
    print("hello", name, "age", age, "score", score);
    print("status", got_age, got_score, "failed", err.failed_now());
    println();
    return 0;
}
