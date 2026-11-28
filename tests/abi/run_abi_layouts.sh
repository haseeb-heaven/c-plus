#!/bin/sh
# Public-type layout agreement between the host C compiler and the C+ driver.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CLANG:-/usr/bin/clang}
mkdir -p build/abi
sh tests/libc/build_libc.sh >/dev/null
sh scripts/build_safecrt.sh >/dev/null
"$CC" -O1 -std=c99 -Wall -Wextra -pedantic \
  -Ilibc -Ilibc/include -Ilibc/install-include -Istdlib/safe \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  tests/abi/abi_layouts.c \
  build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a \
  -o build/abi/abi_layouts
./build/c+ build --sanitize=none tests/abi/abi_layouts.c \
  -Ilibc -Ilibc/install-include -Istdlib/safe \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  -o build/abi/abi_layouts.cplus -- \
  -Lbuild/libc -lcplus_hardened -Lbuild/safe -lcplus_safecrt \
  -Wno-unused-command-line-argument
timeout 30 build/abi/abi_layouts > build/abi/host.out
timeout 30 build/abi/abi_layouts.cplus > build/abi/cplus.out
diff -u build/abi/host.out build/abi/cplus.out
tail -1 build/abi/host.out
