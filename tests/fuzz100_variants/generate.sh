#!/bin/sh
# Generate 100 distinct deterministic C+ example variants for allocator fuzzing.
# Each variant combines: allocator kind (i%4), container (i%2), hostile class
# (i%5), and a fixed seed i. No Python; pure POSIX sh + awk/printf.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
DIR=tests/fuzz100_variants/examples
mkdir -p "$DIR"

i=0
while [ "$i" -lt 100 ]; do
    a=$((i % 4)); c=$((i % 2)); h=$((i % 5))
    f="$DIR/variant_$i.cp"
    {
        printf '/* Generated variant %d: allocator=%d container=%d hostile=%d seed=%d */\n' \
            "$i" "$a" "$c" "$h" "$i"
        printf '#include "cplus/cplus.h"\n'
        printf '#include "stdlib/error.hp"\n'
        printf '#include "stdlib/alloc_iface.hp"\n'
        printf '#include "stdlib/arena.hp"\n'
        printf '#include "stdlib/mempool.hp"\n'
        printf '#include "stdlib/datastack.hp"\n'
        printf '#include "stdlib/string.hp"\n'
        printf '#include "stdlib/vector.hp"\n'
        printf '#include <stdio.h>\n'
        printf 'int main(void) {\n'
        printf '  error e = error();\n'
        printf '  stdlib_set_error_callback((stdlib_error_callback)0);\n'
        printf '  volatile int seed = %d;\n' "$i"
        printf '  int failures = 0;\n'
        case "$a" in
            0) printf '  stdlib_allocator A = stdlib_heap_allocator();\n' ;;
            1) printf '  stdlib_arena ar; stdlib_arena_init(&ar, 4096, &e);\n'
               printf '  stdlib_allocator A = stdlib_arena_as_allocator(&ar);\n' ;;
            2) printf '  stdlib_mem_pool pool; stdlib_mem_pool_init(&pool, 1024, 8, &e);\n'
               printf '  stdlib_allocator A = stdlib_mem_pool_as_allocator(&pool);\n' ;;
            3) printf '  stdlib_datastack *ds = stdlib_thread_datastack(&e);\n'
               printf '  stdlib_allocator A = stdlib_scratch_allocator();\n'
               printf '  (void)ds;\n' ;;
        esac
        case "$c" in
            0) printf '  string s = string_in(&A, "variant");\n'
               printf '  int k; for (k = 0; k < 8 + seed %% 8; k++) if (s.append("x") != 0) failures++;\n'
               printf '  if (s.size() == 0) failures++;\n'
               printf '  s.drop();\n' ;;
            1) printf '  vector v = vector_in(&A);\n'
               printf '  int k; for (k = 0; k < 16 + seed %% 16; k++) if (v.push_back(k + seed) != 0) failures++;\n'
               printf '  if (v.size() == 0 || v.at(0) != seed) failures++;\n'
               printf '  v.drop();\n' ;;
        esac
        case "$h" in
            0) printf '  void *p = stdlib_allocate(&A, (size_t)-1, 8, &e);\n'
               printf '  if (p != (void*)0) failures++;\n' ;;
            1) printf '  void *q = stdlib_allocate(&A, 0, 8, &e);\n'
               printf '  if (q != (void*)0) failures++;\n' ;;
            2) printf '  void *r = stdlib_allocate(&A, 64, 3, &e); /* invalid alignment */\n'
               printf '  if (r != (void*)0) failures++;\n' ;;
            3) printf '  unsigned char stackbuf[32];\n'
               printf '  stdlib_slot_ref sr; sr.pool=(stdlib_slot_pool*)0; sr.index=0; sr.generation=0;\n'
               printf '  if (stdlib_slot_ref_get(&sr, &e) != (void*)0) failures++;\n'
               printf '  (void)stackbuf;\n' ;;
            4) printf '  stdlib_scratch_mark m = stdlib_scratch_enter(stdlib_thread_datastack(&e));\n'
               printf '  void *t = stdlib_allocate(&A, 128, 16, &e);\n'
               printf '  if (t != (void*)0 && (((unsigned long)t)&15ul)!=0) failures++;\n'
               printf '  stdlib_scratch_leave(stdlib_thread_datastack(&e), m);\n' ;;
        esac
        case "$a" in
            1) printf '  stdlib_arena_destroy(&ar, &e);\n' ;;
            2) printf '  stdlib_mem_pool_destroy(&pool, &e);\n' ;;
        esac
        printf '  print("VARIANT", %d, failures);\n' "$i"
        printf '  return failures == 0 ? 0 : 1;\n}\n'
    } > "$f"
    i=$((i+1))
done
echo "GENERATED 100 variants in $DIR"
