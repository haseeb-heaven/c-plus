/* Shared helpers for independent modern-C ports of Goose sample workloads.
 * C11, no Goose code. All programs print deterministic stdout. */
#ifndef GOOSE_PORT_COMMON_H
#define GOOSE_PORT_COMMON_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#define CHECK(label, cond) do { printf("%s %d\n", (label), (cond) ? 1 : 0); \
    if (!(cond)) fails++; } while (0)
static int fails;
static void report(const char *name) {
    printf("%s %s\n", name, fails == 0 ? "PASS" : "FAIL");
}
static int cmp_int(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}
#endif
