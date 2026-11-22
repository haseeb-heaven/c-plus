#!/bin/sh
# Compile-time safety test suite for the modern C+ standard library.
# Each case declares EXPECT: REJECT | ACCEPT | RUNTIME in its manifest.
# Results are reported SEPARATELY from runtime safety tests.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
OUT="$ROOT/build/compile-time-safety"
mkdir -p "$OUT"
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH

python3 - "$ROOT" "$CPLUS" "$OUT" <<'PY'
import json, pathlib, subprocess, sys
root, cplus, out = (pathlib.Path(x) for x in sys.argv[1:4])
manifests = sorted((root/'tests/stdlib/compile_time').glob('*.json'))
if not manifests:
    print('compile-time suite: NO MANIFESTS (agents still writing)')
    sys.exit(1)

stats = {'REJECT': [0,0], 'ACCEPT': [0,0], 'RUNTIME': [0,0]}
gaps = []
rows = []
for mf in manifests:
    data = json.loads(mf.read_text())
    for case in data.get('cases', []):
        name = case['file']
        src = root/'tests/stdlib/compile_time'/name
        expect = case['expect']
        if not src.exists():
            rows.append((name, expect, 'MISSING', 'missing source'))
            continue
        # json.hp includes yyjson.h; supply the same flags the runtime suite uses.
        extra = []
        if name.startswith('json_'):
            extra = ['-I'+str(root/'third_party/yyjson'),
                     '-L'+str(root/'build/ext'), '-lyyjson']
        # Command 1: c+ check --strict
        r1 = subprocess.run([str(cplus), 'check', '--strict'] + extra + [str(src)],
                            capture_output=True, text=True, timeout=60)
        diag = r1.stdout + r1.stderr
        cplus_diag = '[CPLUS-' in diag
        rejected = r1.returncode != 0
        # Command 2: c+ build --strict --sanitize=none
        exe = out/(src.stem+'_strict')
        r2 = subprocess.run([str(cplus), 'build', '--strict', '--sanitize=none'] + extra + [str(src), '-o', str(exe)],
                            capture_output=True, text=True, timeout=60)
        build_rejected = r2.returncode != 0
        if rejected and build_rejected and cplus_diag:
            observed = 'REJECT'
        elif not rejected and not build_rejected:
            observed = 'ACCEPT'
        elif rejected or build_rejected:
            # A rejection without a tagged [CPLUS- diagnostic is a tagging gap,
            # not C+ ownership/lifetime enforcement (suite rule 7).
            observed = 'GAP-UNTAGGED'
        else:
            observed = 'GAP'
        ok = (observed == expect) if expect in ('REJECT','ACCEPT') else True
        key = expect if expect in stats else 'RUNTIME'
        stats[key][1] += 1  # total
        if ok: stats[key][0] += 1  # as expected
        note = case.get('note','')
        # Report capability/tagging gaps explicitly; never fold them into passes.
        if observed.startswith('GAP') or expect == 'RUNTIME' or (expect == 'REJECT' and observed == 'ACCEPT'):
            gaps.append((name, expect, observed))
        rows.append((name, expect, observed, note))

print('case                                   expect   observed  note')
for name, expect, observed, note in rows:
    print(f'{name:38s} {expect:8s} {observed:8s} {note[:60]}')
print('---')
print('compile-time suite (separate from runtime):')
for k in ('REJECT','ACCEPT','RUNTIME'):
    ok, total = stats[k]
    print(f'  {k}: {ok}/{total} as expected')
print('compiler capability / diagnostic-tagging gaps (NOT counted as passes):')
for name, expect, observed in gaps:
    print(f'  {name:38s} expect={expect:8s} observed={observed}')
print(f'  total gap cases: {len(gaps)} (see docs/libc/COMPILE_TIME_GAPS.md)')
print('note: backend warnings without [CPLUS- are not counted as C+ ownership/lifetime enforcement.')
print('note: RUNTIME cases are compiler capability gaps and must be covered by runtime suites separately.')
PY
