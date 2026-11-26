#!/bin/sh
# Reproducible Fil-C comparison on a macOS host with Docker.
#
# The published Fil-C release is Linux-only, so this runner executes the real
# compiler and the binaries it produces inside a Linux aarch64 container with
# the repository mounted at /repo. No numbers are inferred or fabricated.
#
# Usage: sh tests/compiler/cross_compiler/run_filc_docker.sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"
command -v docker >/dev/null || { echo "docker not found; Fil-C NOT_TESTED"; exit 2; }
docker info >/dev/null 2>&1 || { echo "docker daemon unavailable; Fil-C NOT_TESTED"; exit 2; }

REL_URL=https://github.com/pizlonator/fil-c/releases/download/v0.687/filc-0.687-linux-aarch64.tar.xz
TMP=${TMPDIR:-/tmp}/cplus-filc-$$
mkdir -p "$TMP"
trap 'docker rm -f cplus-filc >/dev/null 2>&1 || true; rm -rf "$TMP"' EXIT
curl -sSL -o "$TMP/filc.tar.xz" "$REL_URL"

docker run -d --name cplus-filc -v "$ROOT:/repo" ubuntu:24.04 sleep infinity >/dev/null
docker exec cplus-filc mkdir -p /work
docker cp "$TMP/filc.tar.xz" cplus-filc:/work/filc.tar.xz
docker exec cplus-filc bash -lc \
  'apt-get update -qq >/dev/null 2>&1 && apt-get install -y -qq patchelf xz-utils binutils >/dev/null 2>&1'
docker exec cplus-filc bash -lc \
  'cd /work && tar -xJf filc.tar.xz && mv filc-0.687-linux-aarch64 filc && cd filc && ./setup.sh >/dev/null 2>&1'
docker cp tests/compiler/cross_compiler/filc_compare.sh cplus-filc:/work/filc_compare.sh
docker exec cplus-filc bash /work/filc_compare.sh

python3 - <<'PY'
import datetime, json, os
res = 'tests/compiler/cross_compiler/results/filc_actual/results.tsv'
rows = []
for line in open(res):
    f = line.rstrip('\n').split('\t')
    if len(f) != 5:
        continue
    name, phase, crc, rrc, out = f
    rows.append({'scenario': name,
                 'compile_rc': int(crc) if crc != '-' else None,
                 'run_rc': int(rrc) if rrc != '-' else None,
                 'outcome': 'runtime_trap' if (rrc not in ('0', '-') or
                             'safety error' in out) else 'clean_run',
                 'runtime_output_head': out[:300]})
summary = {
    'generated': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'toolchain': 'Fil-C 0.687 (clang 20.1.8, aarch64 Linux)',
    'host': 'Darwin arm64 via Docker Linux aarch64 container (ubuntu:24.04)',
    'release': 'https://github.com/pizlonator/fil-c/releases/download/v0.687/filc-0.687-linux-aarch64.tar.xz',
    'note': 'Real binaries compiled and executed; not a macOS-native build. Trap-C remains NOT_TESTABLE (no published compiler/source).',
    'results': rows,
}
json.dump(summary, open('tests/compiler/cross_compiler/results/filc_docker_summary.json', 'w'), indent=2)
print('filc_docker:', ', '.join('%s=%s' % (r['scenario'], r['outcome']) for r in rows))
PY
