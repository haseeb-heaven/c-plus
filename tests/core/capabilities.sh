#!/bin/sh
# Probe native compiler/platform capabilities; never claim unsupported checks.
set -u
CXX=${CPLUS_CLANG:-/usr/bin/clang}
echo "# Native capability probe"
echo
echo "- compiler: $("$CXX" --version | head -1)"
printf '%s\n' 'int main(void){return 0;}' > /tmp/cplus_cap.c
if "$CXX" -std=gnu17 -fbounds-safety -fsyntax-only /tmp/cplus_cap.c >/dev/null 2>&1; then
  echo "- Apple bounds safety (-fbounds-safety): supported"
else
  echo "- Apple bounds safety (-fbounds-safety): unsupported by this compiler"
fi
if "$CXX" -std=gnu17 -fsyntax-only -x c - <<'EOF2' >/dev/null 2>&1
#include <stddef.h>
int main(void){ char b[8]; return __builtin_object_size(b,0)==8?0:1; }
EOF2
then
  echo "- __builtin_object_size: supported"
else
  echo "- __builtin_object_size: unsupported"
fi
if ( "$CXX" -std=gnu17 -fsanitize=address /tmp/cplus_cap.c -o /tmp/cplus_cap_asan && /tmp/cplus_cap_asan ) >/dev/null 2>&1; then
  echo "- ASan runtime: working"
else
  echo "- ASan runtime: broken/unavailable on this host"
fi
if ( "$CXX" -std=gnu17 -fsanitize=thread /tmp/cplus_cap.c -o /tmp/cplus_cap_tsan && /tmp/cplus_cap_tsan ) >/dev/null 2>&1; then
  echo "- TSan runtime: working"
else
  echo "- TSan runtime: broken/unavailable on this host"
fi
if printf '#define __STDC_WANT_SECURE_LIB__ 1\n#include <string.h>\nint main(void){char b[4];return strcpy_s(b,4,"x");}\n' | "$CXX" -std=gnu17 -x c -fsyntax-only - >/dev/null 2>&1; then
  echo "- strcpy_s (native CRT): available"
else
  echo "- strcpy_s (native CRT): unavailable (do not emulate silently)"
fi
echo
echo "C+ policy: use installed Clang AST/CFG and supported sanitizers; do not vendor libc/CRT."
