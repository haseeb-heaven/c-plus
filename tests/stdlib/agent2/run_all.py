#!/usr/bin/env python3
"""Agent 2 aggregate regression runner implementation.

Called by run_all.sh. Every result records the exact build/runtime command,
exit codes, captured stdout/stderr, and the classification:
PASS / FAIL / BUILD_FAILURE / TIMEOUT / SKIPPED / BLOCKED.
"""
import datetime, json, os, pathlib, subprocess, sys, shlex

ROOT = pathlib.Path(sys.argv[1]).resolve()
RESULTS = pathlib.Path(sys.argv[2]).resolve()
CPLUS = pathlib.Path(sys.argv[3]).resolve()
BUILD_TIMEOUT = int(sys.argv[4])
RUN_TIMEOUT = int(sys.argv[5])
SELECT_TEST = sys.argv[6]
SELECT_MODULE = sys.argv[7]
CLEAN = sys.argv[8] == '1'
KEEP_LOGS = sys.argv[9] == '1'

STD = ROOT / 'tests/stdlib'
AG = STD / 'agent2'
RESULTS.mkdir(parents=True, exist_ok=True)
if CLEAN:
    for p in RESULTS.iterdir():
        if p.is_file():
            p.unlink()
RUN_DIR = RESULTS / 'run'
if RUN_DIR.exists():
    for p in RUN_DIR.iterdir():
        if p.is_file():
            p.unlink()
RUN_DIR.mkdir(parents=True, exist_ok=True)

ENV = dict(os.environ)
ENV['PATH'] = '/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin'
if 'CPLUS_CLANG' not in ENV:
    ENV['CPLUS_CLANG'] = '/usr/bin/clang'

def discover():
    items = []
    for p in sorted(STD.glob('deep_*.cp')):
        items.append(('deep', p))
    for p in sorted(AG.glob('*.cp')):
        items.append(('agent2', p))
    mf = AG / 'existing_tests.txt'
    if mf.exists():
        for line in mf.read_text().splitlines():
            name = line.strip()
            if not name or name.startswith('#'):
                continue
            p = STD / f'{name}.cp'
            if p.exists():
                items.append(('existing', p))
    return items

def module_of(group, path):
    if group != 'deep':
        return group
    n = path.stem
    for m in ('collections','error','file','fs','io','json','log','net',
              'string','thread','vector','cross'):
        if m in n:
            return m
    return 'other'

def extra_flags(src):
    text = src.read_text(errors='replace')
    out = []
    if 'stdlib/json.hp' in text or 'yyjson.h' in text:
        out += ['-I'+str(ROOT/'third_party/yyjson'),
                '-L'+str(ROOT/'build/ext'), '-lyyjson']
    # Agent 2 cross-module test also includes json.hp and is covered above.
    return out

def run(cmd, timeout, stdin=b''):
    try:
        p = subprocess.run(cmd, input=stdin, capture_output=True,
                           timeout=timeout, env=ENV)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired as e:
        return 124, e.stdout or b'', e.stderr or b''

def classify(brc, rrc, out_match, err_match, exit_match):
    if brc == 124 or brc >= 128:
        return 'TIMEOUT'
    if brc != 0:
        return 'BUILD_FAILURE'
    if rrc == 124 or rrc >= 128:
        return 'TIMEOUT'
    if rrc != 0 or not out_match or not err_match or not exit_match:
        return 'FAIL'
    return 'PASS'

def main():
    if not CPLUS.exists():
        print(f'BLOCKED: compiler missing at {CPLUS}', file=sys.stderr)
        return 1
    all_items = discover()
    selected = []
    for group, src in all_items:
        name = src.stem
        if SELECT_TEST and name != SELECT_TEST:
            continue
        if SELECT_MODULE and module_of(group, src) != SELECT_MODULE:
            continue
        selected.append((group, src))

    records = []
    for group, src in selected:
        name = src.stem
        exe = RUN_DIR / name
        build_log = RESULTS / f'{name}.build.log'
        stdout_log = RESULTS / f'{name}.stdout'
        stderr_log = RESULTS / f'{name}.stderr'
        diff_log = RESULTS / f'{name}.diff'
        cmd_log = RESULTS / f'{name}.commands'
        for p in (exe, build_log, stdout_log, stderr_log, diff_log, cmd_log):
            if p.exists() and p.is_file() and p.name != f'{name}.commands':
                p.unlink()

        extra = extra_flags(src)
        build_cmd = [str(CPLUS), 'build', str(src), '--strict',
                     '--sanitize=none'] + extra + ['-o', str(exe)]
        cmd_log.write_text('BUILD: ' + shlex.join(build_cmd) + '\n')

        brc, bout, berr = run(build_cmd, BUILD_TIMEOUT)
        build_log.write_bytes(bout + b'\n--- stderr ---\n' + berr)

        rec = {
            'name': name, 'group': group, 'module': module_of(group, src),
            'source': str(src.relative_to(ROOT)),
            'build_command': shlex.join(build_cmd), 'build_exit': brc,
            'build_stdout': bout.decode('utf-8', 'replace'),
            'build_stderr': berr.decode('utf-8', 'replace'),
            'expectation': None, 'expected_exit': 0,
            'runtime_command': None, 'runtime_exit': None,
            'stdout': '', 'stderr': '', 'classification': None, 'reason': '',
        }
        if brc != 0:
            rec['classification'] = ('TIMEOUT' if brc == 124 or brc >= 128
                                     else 'BUILD_FAILURE')
            rec['reason'] = f'compiler exit {brc}'
            records.append(rec)
            continue

        stdin_data = b''
        for cand in (STD/f'{name}.stdin', AG/f'{name}.stdin'):
            if cand.exists():
                stdin_data = cand.read_bytes()
                break
        run_cmd = [str(exe)]
        cmd_log.write_text(cmd_log.read_text() +
                           'RUN: ' + shlex.join(run_cmd) + '\n')
        rrc, out, err = run(run_cmd, RUN_TIMEOUT, stdin_data)
        stdout_log.write_bytes(out)
        stderr_log.write_bytes(err)

        exp = None
        exp_err = None
        for base in (src.parent, STD, AG):
            if (base/f'{name}.expected').exists():
                exp = base/f'{name}.expected'
            if (base/f'{name}.stderr.expected').exists():
                exp_err = base/f'{name}.stderr.expected'
        exp_exit = 0
        for base in (src.parent, STD, AG):
            if (base/f'{name}.exit').exists():
                exp_exit = int((base/f'{name}.exit').read_text().strip())
                break

        diffs = []
        if exp is not None:
            want = exp.read_bytes()
            if want != out:
                diffs.append('stdout differs from ' + str(exp.relative_to(ROOT)))
        if exp_err is not None:
            want_err = exp_err.read_bytes()
            if want_err != err:
                diffs.append('stderr differs from ' + str(exp_err.relative_to(ROOT)))
        if diffs:
            diff_log.write_text('\n'.join(diffs) + '\n')
        exit_match = (rrc == exp_exit)
        rec.update({
            'expectation': str(exp.relative_to(ROOT)) if exp else None,
            'expected_exit': exp_exit,
            'runtime_command': shlex.join(run_cmd),
            'runtime_exit': rrc,
            'stdout': out.decode('utf-8', 'replace'),
            'stderr': err.decode('utf-8', 'replace'),
            'classification': classify(brc, rrc, not diffs, True, exit_match),
            'reason': ('; '.join(diffs) if diffs else
                       (f'exit {rrc} != {exp_exit}' if not exit_match else '')),
        })
        records.append(rec)

    counts = {k: 0 for k in ('PASS','FAIL','BUILD_FAILURE','TIMEOUT','SKIPPED','BLOCKED')}
    for r in records:
        counts[r['classification']] = counts.get(r['classification'], 0) + 1

    head = subprocess.run(['git','rev-parse','--short','HEAD'], cwd=ROOT,
                          capture_output=True, text=True).stdout.strip()
    summary = {
        'schema_version': 1,
        'generated_at': datetime.datetime.now().isoformat(),
        'head': head,
        'compiler': str(CPLUS),
        'build_timeout': BUILD_TIMEOUT,
        'run_timeout': RUN_TIMEOUT,
        'counts': counts,
        'total': len(records),
        'results': records,
    }
    (RESULTS/'summary.json').write_text(json.dumps(summary, indent=2) + '\n')

    lines = []
    lines.append(f'Agent 2 aggregate regression summary ({head})')
    lines.append(f'compiler={CPLUS} build_timeout={BUILD_TIMEOUT}s run_timeout={RUN_TIMEOUT}s')
    lines.append('')
    lines.append(f'{"TEST":40s} {"GROUP":9s} {"MODULE":11s} {"CLASS":14s} BUILD RUN DETAIL')
    for r in records:
        lines.append(f'{r["name"]:40s} {r["group"]:9s} {r["module"]:11s} '
                     f'{r["classification"]:14s} {r["build_exit"]:5d} '
                     f'{str(r["runtime_exit"]):3s} {r["reason"][:70]}')
    lines.append('')
    lines.append('SUMMARY ' + ' '.join(f'{k}={v}' for k, v in counts.items()) +
                 f' TOTAL={len(records)}')
    text = '\n'.join(lines) + '\n'
    (RESULTS/'summary.txt').write_text(text)
    sys.stdout.write(text)

    bad = counts['FAIL'] + counts['BUILD_FAILURE'] + counts['TIMEOUT'] + \
          counts['SKIPPED'] + counts['BLOCKED']
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())
