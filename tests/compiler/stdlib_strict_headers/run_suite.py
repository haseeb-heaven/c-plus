#!/usr/bin/env python3
"""Strict-mode stdlib header regression (issue #49)."""
import datetime, json, os, platform, subprocess, sys
ROOT=os.path.abspath(os.path.join(os.path.dirname(__file__),'..','..','..'))
BIN=os.environ.get('CPLUS_BIN',os.path.join(ROOT,'build','c+'))
HERE=os.path.dirname(os.path.abspath(__file__)); OUT=os.path.join(HERE,'results')
os.makedirs(OUT,exist_ok=True)
HEADERS=['error','vector','thread','collections','mempool','arena','scoped_mem']
def run(argv,t=90):
 try:
  p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=t)
  return {'command':argv,'rc':p.returncode,'stdout':p.stdout.decode(errors='replace'),'stderr':p.stderr.decode(errors='replace'),'timed_out':False}
 except subprocess.TimeoutExpired: return {'command':argv,'rc':None,'stdout':'','stderr':'','timed_out':True}
def main():
 rows,fails=[],[]
 for h in HEADERS:
  p=os.path.join(HERE,'include_'+h+'.cp')
  open(p,'w').write('#include "stdlib/%s.hp"\nint main(void){ return 0; }\n'%h)
  c=run([BIN,'check','--strict','--sanitize=none',p])
  ok=c['rc']==0 and not c['timed_out']
  rows.append({'header':h,'status':'PASS' if ok else 'FAIL','steps':[c]})
  if not ok: fails.append(h)
  print('%-14s %s'%(h,'PASS' if ok else 'FAIL'))
 payload={'generated':datetime.datetime.now(datetime.timezone.utc).isoformat(),'host':platform.platform(),'compiler':BIN,'results':rows,'failures':fails}
 json.dump(payload,open(os.path.join(OUT,'summary.json'),'w'),indent=2)
 open(os.path.join(OUT,'summary.txt'),'w').write('Strict stdlib headers\n%s\n%s\n\n'%(payload['host'],BIN)+''.join('%s %s\n'%(r['header'],r['status']) for r in rows)+'\ncases=%d passed=%d failed=%d\n'%(len(rows),len(rows)-len(fails),len(fails)))
 print('stdlib_strict_headers: %d cases, %d failures'%(len(rows),len(fails)))
 return 1 if fails else 0
if __name__=='__main__': sys.exit(main())
