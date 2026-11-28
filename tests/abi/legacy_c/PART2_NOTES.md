# Legacy C ABI corpus — Part 2 (lc26–lc50) verification notes

This document records the evidence for the **second half** of the 50-program
legacy C ABI corpus: 25 independent C99/C11 programs `lc26_*.c` … `lc50_*.c`
plus this note. Every program has its own `main`, prints exactly one stable
`lcNN result: OK ...` line, and exits `0` only when all of its checks pass.

No files outside `tests/abi/legacy_c/lc[26-50]_*.c` and this note were created,
modified, staged or committed. In particular the Part 1 files (`lc01_*.c` …
`lc25_*.c`, owned by a parallel agent) were left untouched; the wildcard in the
first exploratory loop happened to include `lc20`–`lc25` but the recorded final
verification below is restricted to the 25 files owned by this task.

## 1. Environment (captured 2026-11-23T04:00:49Z)

| item | value |
| --- | --- |
| host | `arm64` / `Darwin` (arm64-apple-darwin27.2.0) |
| compiler | `/usr/bin/clang` — Apple clang version 16.0.0 (clang-1600.0.26.6) |
| thread model | posix |
| standard / flags | `-std=c11 -Wall -Wextra -pedantic`, then `-O0` and `-O2` |
| runner | `/usr/bin/timeout 10` (GNU-style timeout present at `/usr/bin/timeout`; verified `rc=124` on a 5 s sleep with a 2 s limit) |
| artifacts | temporary directory `/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcpart2` (outside the repository) |

## 2. Files created (25 programs)

| file | coverage |
| --- | --- |
| `lc26_global_vars.c` | global variables: external/internal linkage, static storage, aggregate init |
| `lc27_global_aggregate.c` | global struct/array plus a pointer aliasing a global object |
| `lc28_thread_local_basic.c` | C11 `_Thread_local` scalar and array, address stability |
| `lc29_thread_local_accessors.c` | C11 TLS via exported-style accessors, TLS vs global separation |
| `lc30_opaque_handle.c` | opaque handle (incomplete type), create/sum/validate/destroy |
| `lc31_opaque_state_machine.c` | opaque handle with retain/release and aliasing |
| `lc32_stdio_interop.c` | libc stdio: `snprintf`, `sscanf`, format/parse round trip |
| `lc33_stdlib_interop.c` | libc stdlib: `qsort` callback, `strtol`, `abs`, `ldiv` |
| `lc34_string_interop.c` | libc string: `strlen`, `strcat`, `strcmp`, `strchr`, `memcpy`, `memmove`, `memset`, `memcmp` |
| `lc35_allocator_interop.c` | allocator: `malloc`, `realloc` grow/shrink with preservation checks, `free` |
| `lc36_calloc_zeroed.c` | allocator: `calloc` zeroing, `memset`, `realloc`, `free` |
| `lc37_file_handle_roundtrip.c` | file handles: `tmpfile`, `fwrite`, `rewind`, `fread`, `fclose` |
| `lc38_file_seek_tell.c` | file handles: `fseek`, `ftell`, `fgetc`, `feof`, EOF handling |
| `lc39_string_buffer_build.c` | string buffers: fixed-capacity builder with offset and NUL maintenance |
| `lc40_string_buffer_dynamic.c` | string buffers: two-pass `snprintf` sizing, truncation, heap buffer |
| `lc41_exported_functions.c` | shared-library boundary simulation: externally linked functions reached through a function-pointer dispatch table |
| `lc42_exported_vtable.c` | shared-library boundary simulation: exported factory + C vtable of function pointers |
| `lc43_bitfields.c` | struct patterns: bitfields, zero-width alignment, modulo truncation |
| `lc44_tagged_union.c` | union patterns: tagged union, typed member accessors, `memcpy` payload writes |
| `lc45_pointer_arithmetic.c` | pointer patterns: arithmetic, `ptrdiff_t` difference, array decay |
| `lc46_function_pointers.c` | pointer patterns: function pointer arrays and pointer-to-function-pointer |
| `lc47_layout_memcpy.c` | struct patterns: `offsetof`, byte-buffer round trip via `memcpy` |
| `lc48_flexible_array.c` | struct patterns: C99 flexible array member in one allocation |
| `lc49_linked_list.c` | pointer patterns: linked list allocation, traversal and teardown |
| `lc50_state_machine.c` | combined: opaque context, tagged union events, handler table, state machine |

## 3. Exact verification commands and results

### 3.1 Compile-and-run loop (both optimization levels)

Working directory: repository root. The loop below was executed verbatim;
`$TMPV` is the temporary directory from section 1. Compiler stdout/stderr was
captured to `$b$opt.compile.log` and measured in bytes (`warn_bytes` below);
`run_rc` is the exit status of `/usr/bin/timeout 10 <binary>`.

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
TMPV=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcpart2
mkdir -p "$TMPV"
: > "$TMPV/results.txt"
fails=0
for f in tests/abi/legacy_c/lc[345][0-9]_*.c tests/abi/legacy_c/lc2[6789]_*.c; do
  b=$(basename "$f" .c)
  for opt in -O0 -O2; do
    /usr/bin/clang -std=c11 -Wall -Wextra -pedantic "$opt" "$f" -o "$TMPV/$b$opt" >"$TMPV/$b$opt.compile.log" 2>&1
    crc=$?
    cw=$(wc -c < "$TMPV/$b$opt.compile.log" | tr -d ' ')
    out=$(/usr/bin/timeout 10 "$TMPV/$b$opt" 2>&1)
    rrc=$?
    printf '%s %s compile_rc=%s warn_bytes=%s run_rc=%s | %s\n' "$b" "$opt" "$crc" "$cw" "$rrc" "$out" | tee -a "$TMPV/results.txt"
    if [ "$crc" -ne 0 ] || [ "$cw" -ne 0 ] || [ "$rrc" -ne 0 ]; then fails=$((fails+1)); fi
  done
done
echo "TOTAL_FAILS=$fails"
```

**Result: `TOTAL_FAILS=0`** — 25 files × 2 optimization levels = 50 compile/run
pairs; every `compile_rc=0`, every `warn_bytes=0` (zero compiler diagnostics),
every `run_rc=0`. Captured transcript (verbatim from `$TMPV/results.txt`):

```
lc26_global_vars -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc26 result: OK counter=20 total=20 epoch=7 label=lc26
lc26_global_vars -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc26 result: OK counter=20 total=20 epoch=7 label=lc26
lc27_global_aggregate -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc27 result: OK id=27 retries=7 mask=a5a5 sum=24
lc27_global_aggregate -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc27 result: OK id=27 retries=7 mask=a5a5 sum=24
lc28_thread_local_basic -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc28 result: OK a=3 b=6 counter=6 sum=10
lc28_thread_local_basic -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc28 result: OK a=3 b=6 counter=6 sum=10
lc29_thread_local_accessors -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc29 result: OK p1=0 p2=11 tls=20 old=22 global=2
lc29_thread_local_accessors -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc29 result: OK p1=0 p2=11 tls=20 old=22 global=2
lc30_opaque_handle -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc30 result: OK id=30 sum=195 destroyed
lc30_opaque_handle -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc30 result: OK id=30 sum=195 destroyed
lc31_opaque_state_machine -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc31 result: OK s1=5 s2=12 r1=1 r2=0
lc31_opaque_state_machine -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc31 result: OK s1=5 s2=12 r1=1 r2=0
lc32_stdio_interop -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc32 result: OK text='lc32:32/97' name=lc32 id=32 score=97
lc32_stdio_interop -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc32 result: OK text='lc32:32/97' name=lc32 id=32 score=97
lc33_stdlib_interop -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc33 result: OK min=-3 max=9 sumsq=168 parsed=-42 quot=7 rem=7
lc33_stdlib_interop -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc33 result: OK min=-3 max=9 sumsq=168 parsed=-42 quot=7 rem=7
lc34_string_interop -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc34 result: OK text='lc34-string' len=11 move='Xlc34-string'
lc34_string_interop -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc34 result: OK text='lc34-string' len=11 move='Xlc34-string'
lc35_allocator_interop -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc35 result: OK grew=8 shrunk=3 first=100u last=102u
lc35_allocator_interop -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc35 result: OK grew=8 shrunk=3 first=100u last=102u
lc36_calloc_zeroed -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc36 result: OK zeroed=1 fill=5a capacity=24
lc36_calloc_zeroed -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc36 result: OK zeroed=1 fill=5a capacity=24
lc37_file_handle_roundtrip -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc37 result: OK records=3 id3=3 value3=3.5
lc37_file_handle_roundtrip -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc37 result: OK records=3 id3=3 value3=3.5
lc38_file_seek_tell -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc38 result: OK size=5 seek=2 value=30 eof=1
lc38_file_seek_tell -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc38 result: OK size=5 seek=2 value=30 eof=1
lc39_string_buffer_build -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc39 result: OK len=11 text='lc39:buffer'
lc39_string_buffer_build -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc39 result: OK len=11 text='lc39:buffer'
lc40_string_buffer_dynamic -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc40 result: OK needed=18 text='lc40/12-34/payload'
lc40_string_buffer_dynamic -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc40 result: OK needed=18 text='lc40/12-34/payload'
lc41_exported_functions -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc41 result: OK sum=13 product=42 version=lc41-v1
lc41_exported_functions -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc41 result: OK sum=13 product=42 version=lc41-v1
lc42_exported_vtable -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc42 result: OK v1=41 v2=42 v3=40
lc42_exported_vtable -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc42 result: OK v1=41 v2=42 v3=40
lc43_bitfields -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc43 result: OK mode=5 count=12 extra=a5 size=8
lc43_bitfields -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc43 result: OK mode=5 count=12 extra=a5 size=8
lc44_tagged_union -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc44 result: OK i=44 d=2.75 b0=1 usize=8
lc44_tagged_union -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc44 result: OK i=44 d=2.75 b0=1 usize=8
lc45_pointer_arithmetic -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc45 result: OK dist=5 steps=5 sum=75 last=25
lc45_pointer_arithmetic -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc45 result: OK dist=5 steps=5 sum=75 last=25
lc46_function_pointers -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc46 result: OK add=24 sub=16 mul=80 indirect=42
lc46_function_pointers -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc46 result: OK add=24 sub=16 mul=80 indirect=42
lc47_layout_memcpy -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc47 result: OK size=24 offsets=0/4/8/16 tag=S count=47
lc47_layout_memcpy -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc47 result: OK size=24 offsets=0/4/8/16 tag=S count=47
lc48_flexible_array -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc48 result: OK header=16 len=6 kind=48
lc48_flexible_array -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc48 result: OK header=16 len=6 kind=48
lc49_linked_list -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc49 result: OK nodes=8 sum=36 head=8
lc49_linked_list -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc49 result: OK nodes=8 sum=36 head=8
lc50_state_machine -O0 compile_rc=0 warn_bytes=0 run_rc=0 | lc50 result: OK accepted=4 current=42 trans=4 history=SARA
lc50_state_machine -O2 compile_rc=0 warn_bytes=0 run_rc=0 | lc50 result: OK accepted=4 current=42 trans=4 history=SARA
```

### 3.2 Shared-library boundary simulation: object + symbol check

The two boundary programs were also compiled to object files. `nm -g`
confirms their API functions have **external (`T`) linkage**, i.e. they are
exported from the object just as they would be from a shared library. At
runtime each program reaches them via a function-pointer table (dispatch table
in `lc41`, vtable in `lc42`), which is the simulated ABI boundary.

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
TMPV=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/lcpart2
for n in lc41_exported_functions lc42_exported_vtable; do
  /usr/bin/clang -std=c11 -Wall -Wextra -pedantic -O0 -c "tests/abi/legacy_c/$n.c" -o "$TMPV/$n.o"
  echo "compile -c rc=$? file=$n.o"
  /usr/bin/nm -g "$TMPV/$n.o" | grep " T "
done
```

Observed output (both `-c` steps exited `0`):

```
compile -c rc=0 file=lc41_exported_functions.o
0000000000000000 T _lc41_add
0000000000000020 T _lc41_mul
0000000000000040 T _lc41_version
000000000000004c T _main
compile -c rc=0 file=lc42_exported_vtable.o
0000000000000038 T _lc42_bump
000000000000008c T _lc42_create
0000000000000068 T _lc42_destroy
0000000000000000 T _lc42_get
0000000000000018 T _lc42_set
00000000000000fc T _main
```

## 4. Corrections made during verification (no fabricated results)

The first exploratory run (wildcard `lc[2-5][0-9]_*.c`) exposed four defects in
this part, all fixed before the final run recorded above:

1. `lc27` expected checksum was `30`, but the actual init+mutation yields `24` (`1+2+3+4+5+9`); expectation corrected to `24UL`.
2. `lc41` checked `version[3] == '4'`, but `"lc41-v1"[3]` is `'1'`; corrected to `version[2] == '4'`.
3. `lc43` assigned the literal `9u` to a 3-bit field, producing `-Wbitfield-constant-conversion`; changed to a non-constant `unsigned int wrap = 9u;` source. Final builds have zero diagnostics.
4. `lc50` history expectation was `"SARS"`, but events are S/A/R/A → `"SARA"`; corrected.

Additionally, an early draft of `lc30`/`lc35` inspected memory after `free`;
both were rewritten to record validation results before release. No program in
the final set dereferences freed storage.

## 5. Summary

- **25** independent C programs created (`lc26_*.c` … `lc50_*.c`), each with `main` and one stable result line.
- **50** compile+run checks (25 files × `-O0`/`-O2`): **50 passed, 0 failed**.
- **0** compiler warnings/errors under `-std=c11 -Wall -Wextra -pedantic`.
- All runs completed well within `/usr/bin/timeout 10` (no timeouts).
- Object/symbol check confirms exported (`T`) ABI functions for both boundary programs.
- No repository files outside the 26 owned paths were touched.
