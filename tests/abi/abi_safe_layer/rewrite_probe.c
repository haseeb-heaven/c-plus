/* Preprocessor-only probe.  It is NEVER compiled/linked: it exists so the
 * runner can show, via `clang -E` and `clang -dM -E`, exactly which ordinary C
 * spellings the force-included header rewrites (and that
 * -DCPLUS_SAFE_C_DISABLE rewrites none of them). */
int abi_rewrite_probe(void)
{
    void *p = malloc(8);
    unsigned char *q = (unsigned char *)calloc(4, 2);
    memset(p, 0, 8);
    memcpy(q, p, 4);
    memmove(q, p, 4);
    memcmp(q, p, 4);
    strcpy((char *)q, "x");
    strncpy((char *)q, "xy", 2);
    strcat((char *)q, "y");
    strncat((char *)q, "z", 1);
    free(q);
    free(p);
    return 0;
}
