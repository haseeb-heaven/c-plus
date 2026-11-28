#!/bin/sh
# C and C++ consumer compatibility matrix for the nine ISO C headers.
# Builds from a clean installed SDK prefix; no source-tree include paths.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CLANG:-$(command -v clang || command -v gcc)}
CXX=${CPLUS_CXX:-$(command -v clang++ || command -v g++)}
OUT="$ROOT/build/compat"
rm -rf "$OUT"
mkdir -p "$OUT"

# Install the SDK into a clean prefix.
PREFIX="$OUT/prefix"
cmake -S libc -B "$OUT/cmake" -DCMAKE_INSTALL_PREFIX="$PREFIX" >"$OUT/cmake.log" 2>&1 || {
  echo "FAIL cmake configure"; tail -10 "$OUT/cmake.log"; exit 1; }
cmake --build "$OUT/cmake" --parallel >"$OUT/build.log" 2>&1 || {
  echo "FAIL cmake build"; tail -10 "$OUT/build.log"; exit 1; }
cmake --install "$OUT/cmake" >"$OUT/install.log" 2>&1 || {
  echo "FAIL cmake install"; tail -10 "$OUT/install.log"; exit 1; }

INC="-I$PREFIX/include/cplus-libc"
LIB="$PREFIX/lib/libcplus_hardened_libc.a"
pass=0; fail=0

check() {
  label=$1; shift
  if "$@" >"$OUT/$label.log" 2>&1; then
    echo "ok   $label: $(tail -1 "$OUT/$label.run" 2>/dev/null)"
    pass=$((pass + 1))
  else
    echo "FAIL $label"; tail -12 "$OUT/$label.log"; fail=$((fail + 1))
  fi
}

# System-header-only consumers (standard spellings, no overlay).
for std in c99 c11; do
  if $CC -std=$std -Wall -Wextra -pedantic tests/compat/c_headers.c -o "$OUT/c_$std" >"$OUT/c_$std.log" 2>&1 \
     && "$OUT/c_$std" >"$OUT/c_$std.run" 2>&1; then
    echo "ok   c_headers ($std, system headers): $(tail -1 "$OUT/c_$std.run")"; pass=$((pass+1))
  else
    echo "FAIL c_headers ($std, system headers)"; tail -12 "$OUT/c_$std.log"; fail=$((fail+1))
  fi
done

if $CXX -std=c++20 -Wall -Wextra -pedantic tests/compat/cpp_headers.cpp -o "$OUT/cpp20" >"$OUT/cpp20.log" 2>&1 \
   && "$OUT/cpp20" >"$OUT/cpp20.run" 2>&1; then
  echo "ok   cpp_headers (c++20, system headers): $(tail -1 "$OUT/cpp20.run")"; pass=$((pass+1))
else
  echo "FAIL cpp_headers (c++20, system headers)"; tail -12 "$OUT/cpp20.log"; fail=$((fail+1))
fi

# Installed-SDK consumers: include the public header only from the prefix.
for std in c99 c11; do
  if $CC -std=$std -Wall -Wextra -pedantic $INC tests/compat/c_headers.c "$LIB" -lm -pthread \
       -o "$OUT/sdk_c_$std" >"$OUT/sdk_c_$std.log" 2>&1 \
     && "$OUT/sdk_c_$std" >"$OUT/sdk_c_$std.run" 2>&1; then
    echo "ok   c_headers ($std, installed SDK): $(tail -1 "$OUT/sdk_c_$std.run")"; pass=$((pass+1))
  else
    echo "FAIL c_headers ($std, installed SDK)"; tail -12 "$OUT/sdk_c_$std.log"; fail=$((fail+1))
  fi
done

if $CXX -std=c++20 -Wall -Wextra -pedantic $INC tests/compat/cpp_headers.cpp "$LIB" -lm -pthread \
     -o "$OUT/sdk_cpp20" >"$OUT/sdk_cpp20.log" 2>&1 \
   && "$OUT/sdk_cpp20" >"$OUT/sdk_cpp20.run" 2>&1; then
  echo "ok   cpp_headers (c++20, installed SDK): $(tail -1 "$OUT/sdk_cpp20.run")"; pass=$((pass+1))
else
  echo "FAIL cpp_headers (c++20, installed SDK)"; tail -12 "$OUT/sdk_cpp20.log"; fail=$((fail+1))
fi

echo "compat matrix: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
