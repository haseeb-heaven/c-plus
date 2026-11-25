#!/bin/sh
# Deterministic fixture generator for examples/utilities/file_tools.
#
#   sh examples/utilities/file_tools/fixtures/generate.sh <target-dir>
#
# Creates every fixed-byte fixture used by run.sh inside <target-dir>.  No
# clock, network, user home path or external input is involved: the same
# generator always writes the same bytes.  The target directory is expected to
# be a scratch directory owned by the harness (run.sh passes its .run/fixtures).
set -eu

D=${1:?usage: generate.sh <target-dir>}
mkdir -p "$D" || exit 2

# Small text fixtures (trailing newlines are intentional and fixed).
printf 'hello\n'              > "$D/hello.txt"
printf 'alpha\nbeta\ngamma\n' > "$D/alpha.txt"
printf 'ALPHA\nbeta\ngamma\n' > "$D/alpha2.txt"
: > "$D/empty.txt"

# Exact-fit / one-over binaries for fhash (cap 4096) and fcopy (cap 1024).
head -c 4096 /dev/zero > "$D/hash_exact.bin"
head -c 4097 /dev/zero > "$D/hash_over.bin"
head -c 1024 /dev/zero > "$D/copy_exact.bin"
head -c 1025 /dev/zero > "$D/copy_over.bin"

# Oversized byte-count fixture for fcat (cap 2048).
head -c 2049 /dev/zero > "$D/cat_over.bin"

# One overlong line (129 bytes) and one overlong file (33 lines).
i=0
: > "$D/longline.txt"
while [ "$i" -lt 129 ]; do printf 'x' >> "$D/longline.txt"; i=$((i + 1)); done
printf '\n' >> "$D/longline.txt"
i=0
: > "$D/manylines.txt"
while [ "$i" -lt 33 ]; do printf 'line%d\n' "$i" >> "$D/manylines.txt"; i=$((i + 1)); done

# Fixed directory tree for fsize (recursive-ish paths via argv).
mkdir -p "$D/tree/sub" || exit 2
printf '0123456789'                    > "$D/tree/a.bin"
printf '01234567890123456789'           > "$D/tree/b.bin"
printf '012345678901234567890123456789' > "$D/tree/sub/c.bin"

exit 0
