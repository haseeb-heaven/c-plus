#!/usr/bin/env python3
"""Regression for issue #102/#103: checked 64-bit shift width semantics.

Each case is a separate child process because the expected outcome is a
deterministic runtime fatal (exit 110).
"""
import datetime, json, os, platform, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'results')
os.makedirs(OUT, exist_ok=True)
CC = os.environ.get('CC', '/usr/bin/clang')
LIB = os.path.join(ROOT, 'build', 'libcplus_runtime.a')

SRC = r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
extern int64_t __cplus_checked_shr_i64(int64_t,int64_t,unsigned,unsigned);
extern int64_t __cplus_checked_shl_i64(int64_t,int64_t,unsigned,unsigned);
int main(int argc, char **argv) {
  int k = argc > 1 ? atoi(argv[1]) : 0;
  if (k == 0) printf("%lld\n", (long long)__cplus_checked_shr_i64(-1,1,0,0));
  if (k == 1) printf("%lld\n", (long long)__cplus_checked_shr_i64(-1,1,65,0));
  if (k == 2) printf("%lld\n", (long long)__cplus_checked_shr_i64(-1,1,0,1));
  if (k == 3) printf("%lld\n", (long long)__cplus_checked_shl_i64(256,1,8,0));
  if (k == 4) printf("%lld\n", (long long)__cplus_checked_shl_i64(0x80000000LL,1,32,0));
  if (k == 5) printf("%lld\n", (long long)__cplus_checked_shl_i64(1,2,64,1));
  return 0;
}
'''

CASES = [
    ('unsigned_shr_bits0_logical', '0', 0, '9223372036854775807\n'),
    ('unsigned_shr_bits65_logical', '1', 0, '9223372036854775807\n'),
    ('signed_shr_bits0_arithmetic', '2', 0, '-1\n'),
    ('unsigned_shl_8bit_overflow_traps', '3', 110, 'invalid shift'),
    ('unsigned_shl_32bit_overflow_traps', '4', 110, 'invalid shift'),
    ('signed_shl_in_range', '5', 0, '4\n'),
]


def run(argv):
    try:
        p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=60)
        return {'command': argv, 'rc': p.returncode,
                'stdout': p.stdout.decode(errors='replace'),
                'stderr': p.stderr.decode(errors='replace')}
    except subprocess.TimeoutExpired:
        return {'command': argv, 'rc': None, 'stdout': '', 'stderr': 'TIMEOUT'}


def main():
    src = os.path.join(HERE, 'shift_width.c')
    with open(src, 'w') as f:
        f.write(SRC)
    exe = os.path.join(OUT, 'shift_width.bin')
    steps = [run([CC, '-std=c11', src, LIB, '-o', exe, '-lpthread', '-lm'])]
    ok = steps[0]['rc'] == 0
    results = []
    if ok:
        for name, arg, want_rc, want_text in CASES:
            r = run([exe, arg])
            steps.append(r)
            good = (r['rc'] == want_rc and
                    want_text in (r['stdout'] + r['stderr']))
            results.append({'case': name, 'status': 'PASS' if good else 'FAIL',
                            'expected_rc': want_rc, 'expected_text': want_text,
                            'result': r})
            ok = ok and good
    payload = {'generated': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'host': platform.platform(), 'cc': CC, 'results': results,
               'steps': steps}
    json.dump(payload, open(os.path.join(OUT, 'summary.json'), 'w'), indent=2)
    with open(os.path.join(OUT, 'summary.txt'), 'w') as f:
        for row in results:
            f.write('%s %s\n' % (row['case'], row['status']))
    print('runtime_shift_width:', '%d cases, %d failures' %
          (len(results), sum(1 for r in results if r['status'] == 'FAIL')))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
