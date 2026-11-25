# Windows x86-64 execution evidence via highball + Wine

**Claim boundary.** This is a Windows *target* check, not a claim that the
`c+` driver binary itself runs on Windows. The host compiler remains macOS/
Linux; `c+` lowered a C+ program to C, a Windows PE32+ executable was produced
by MinGW-w64, and that unchanged executable was run under Highball's Wine
engine. The native C ABI was preserved: the C+ lowered program and an ordinary
C program produced byte-identical stdout.

## Exact environment

- Host: macOS 27.2.1, arm64 (Apple silicon).
- `c+` 0.1.0, backend Homebrew Clang/LLVM 23.1.6.
- `x86_64-w64-mingw32-gcc (GCC) 16.2.0`.
- `Highball CLI 0.10.22 (bridge)`, engine `x64-crossover26.3-r27`, bottle
  `Games`, renderer `dxvk`, msync enabled.
- Wine binary: `~/Library/Application Support/Highball/engines/x64-crossover26.3-r27/engine/bin/wine`.

## Commands and observed output

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-core
./build/c+ check --strict --sanitize=none --emit-c /tmp/winprobe/hello_cp.c /tmp/winprobe/hello_cp.cp
# check: /tmp/winprobe/hello_cp.cp ok

x86_64-w64-mingw32-gcc -std=gnu17 -O2 -I$PWD/runtime/include \
  /tmp/winprobe/hello_cp.c -o /tmp/winprobe/hello_cp.exe
file /tmp/winprobe/hello_cp.exe
# PE32+ executable (console) x86-64, for MS Windows

highball run --bottle Games /tmp/winprobe/hello_cp.exe
# len=4 sum=5
# exit=0 after 4s -- log: .../20261125T055030Z-Games-hello_cp.exe.log
```

Source:

```c
#include <stdio.h>
int main(void){ int a[4]={1,2,3,4};
  printf("len=%d sum=%d\n", a.len, a[0]+a[3]); return 0; }
```

Lowered C (proving no ABI/type change; `.len` becomes a constant):

```c
printf("len=%d sum=%d\n", (4), a[0]+a[3]);
```

Ordinary-C control:

```sh
x86_64-w64-mingw32-gcc -std=c17 -O2 /tmp/winprobe/hello_c.c -o /tmp/winprobe/hello_c.exe
highball run --bottle Games /tmp/winprobe/hello_c.exe
# len=4 sum=5
# exit=0 after 1s -- log: .../20261125T055051Z-Games-hello_c.exe.log
```

Wine emitted expected bottle-startup noise (`winemenubuilder.exe` 126 and a
missing `fwpkclnt.sys` driver for `idmwfp.sys`); neither affected the program,
which printed the expected stdout and exited 0. Those messages are recorded
verbatim in the two Highball log files named above.

## What this does and does not prove

- **Proved:** the C+ lowering is portable C that MinGW-w64 compiles to a
  runnable Windows x86-64 PE; the native C representation and printf ABI are
  unchanged; strict static analysis passed on the source before lowering.
- **Not proved:** the `c+` driver itself runs on Windows; the C+ runtime
  static/shared library links under MSVC or MinGW; sanitizers work under Wine;
  cross-TU/CRT edge cases. Those remain OPEN in
  `docs/compiler/portability.md`. No platform is declared supported from this
  single target smoke test.
