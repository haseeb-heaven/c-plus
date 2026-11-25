#!/usr/bin/env python3
"""Return-escape regression suite (issues #36 and #37).

Negative cases must be rejected by STRICT; positive controls must build and
run with exact stdout. Exact commands, exit codes, stdout and stderr recorded.

Run: CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/return_escape/run_suite.py
"""
import datetime, json, os, platform, subprocess, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BIN = os.environ.get("CPLUS_BIN", os.path.join(ROOT, "build", "c+"))
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "results")
os.makedirs(OUT, exist_ok=True)
NEG = [
 ("neg_struct_field_local", "tests/compiler/safety_breakers/proof_agent1_struct_local_escape.c"),
 ("neg_this_escape", "tests/compiler/safety_breakers/proof_agent1_this_escape.cp"),
]
POS_SRC = {
 "pos_return_by_value": '#include <stdio.h>\nstruct V { int x; };\nstatic struct V make(void){ struct V v={7}; return v; }\nint main(void){ printf("%d\\n", make().x); return 0; }\n',
 "pos_return_heap": '#include <stdio.h>\n#include <stdlib.h>\nstatic int *make(void){ int *p=malloc(sizeof(int)); if(p)*p=7; return p; }\nint main(void){ int *p=make(); printf("%d\\n",*p); free(p); return 0; }\n',
}

def run(argv, timeout=60):
    try:
        p=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=timeout)
        return {"command":argv,"rc":p.returncode,"stdout":p.stdout.decode(errors="replace"),
                "stderr":p.stderr.decode(errors="replace"),"timed_out":False}
    except subprocess.TimeoutExpired:
        return {"command":argv,"rc":None,"stdout":"","stderr":"","timed_out":True}

def main():
    rows,fails=[],[]
    for name,rel in NEG:
        c=run([BIN,"check","--strict","--sanitize=none",os.path.join(ROOT,rel)])
        ok=c["rc"] not in (0,None) and "CPLUS-LIFETIME" in c["stderr"]
        rows.append({"case":name,"status":"PASS" if ok else "FAIL","expected":"CPLUS-LIFETIME","steps":[c]})
        if not ok: fails.append(name)
        print("%-24s %s"%(name,"PASS" if ok else "FAIL"))
    for name,src in POS_SRC.items():
        p=os.path.join(HERE,name+".c"); open(p,"w").write(src)
        exe=os.path.join(OUT,name+".bin")
        b=run([BIN,"build","--strict","--sanitize=none",p,"-o",exe]); steps=[b]
        ok=b["rc"]==0 and os.path.exists(exe)
        if ok:
            r=run([exe]); steps.append(r); ok=r["rc"]==0 and r["stdout"]=="7\n"
        rows.append({"case":name,"status":"PASS" if ok else "FAIL","expected":"7","steps":steps})
        if not ok: fails.append(name)
        print("%-24s %s"%(name,"PASS" if ok else "FAIL"))
    payload={"generated":datetime.datetime.now(datetime.timezone.utc).isoformat(),
             "host":platform.platform(),"compiler":BIN,"results":rows,"failures":fails}
    json.dump(payload,open(os.path.join(OUT,"summary.json"),"w"),indent=2)
    with open(os.path.join(OUT,"summary.txt"),"w") as f:
        f.write("Return escape suite\n%s\n%s\n\n"%(payload["host"],BIN))
        for r in rows:
            f.write("%-24s %s\n"%(r["case"],r["status"]))
            for s in r["steps"]:
                f.write("  cmd: %s\n  rc=%s out=%r err=%r\n"%(" ".join(s["command"]),s["rc"],s["stdout"][:120],s["stderr"][:300]))
        f.write("\ncases=%d passed=%d failed=%d\n"%(len(rows),len(rows)-len(fails),len(fails)))
    print("return_escape: %d cases, %d failures"%(len(rows),len(fails)))
    return 1 if fails else 0
if __name__=="__main__": sys.exit(main())
