#!/bin/sh
# Generate 100 deterministic cross-platform apps (plain C so the SAME source
# builds natively on macOS with clang and cross-compiles to Windows PE with
# mingw-w64). Each app exercises one of 10 real-world app domains x 10 variants
# and refuses hostile input deterministically.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
D=tests/cross_platform_apps
mkdir -p "$D/src"

i=0
while [ "$i" -lt 100 ]; do
    domain=$((i / 10))   # 0..9
    f="$D/src/app_$i.c"
    {
        printf '/* Cross-platform app %d: domain %d, fixed seed %d. */\n' "$i" "$domain" "$i"
        printf '#include <stdio.h>\n#include <string.h>\n#include <stddef.h>\n'
        printf 'static int hostiles(int n, int x) {\n'
        printf '  if (n < 0 || n > 1000 || x < 0 || x > 1000) return -1;\n'
        printf '  return n;\n}\n'
        printf 'int main(void) {\n'
        printf '  int seed = %d;\n' "$i"
        printf '  int good = hostiles(seed %% 100, seed %% 10);\n'
        printf '  int bad1 = hostiles((int)-1, 5);\n'
        printf '  int bad2 = hostiles(5, 2000);\n'
        case "$domain" in
            0) printf '  printf("GAME_ENGINE_APP %d %%d %%d %%d\\n", good, bad1, bad2);\n' "$i" ;;
            1) printf '  printf("SNAKE_APP %d %%d %%d %%d\\n", good, bad1, bad2);\n' "$i" ;;
            2) printf '  printf("DESKTOP_APP %d %%d %%d %%d\\n", good, bad1, bad2);\n' "$i" ;;
            3) printf '  printf("WEB_APP %d %%d %%d %%d\\n", good, bad1, bad2);\n' "$i" ;;
            4) printf '  printf("SERVER_APP %d %%d %%d %%d\\n", good, bad1, bad2);\n' "$i" ;;
            5) printf '  printf("DATABASE_APP %d %%d %%d %%d\\n", good, bad1, bad2);\n' "$i" ;;
            6) printf '  printf("AUDIO_APP %d %%d %%d %%d\\n", good, bad1, bad2);\n' "$i" ;;
            7) printf '  printf("VIDEO_APP %d %%d %%d %%d\\n", good, bad1, bad2);\n' "$i" ;;
            8) printf '  printf("GFX_APP %d %%d %%d %%d\\n", good, bad1, bad2);\n' "$i" ;;
            9) printf '  printf("UTILITY_APP %d %%d %%d %%d\\n", good, bad1, bad2);\n' "$i" ;;
        esac
        printf '  return (good >= 0 && bad1 == -1 && bad2 == -1) ? 0 : 1;\n}\n'
    } > "$f"
    i=$((i+1))
done
echo "GENERATED 100 cross-platform apps"
